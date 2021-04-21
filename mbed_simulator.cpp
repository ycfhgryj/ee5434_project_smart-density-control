#include "mbed.h"
#include "mbed_trace.h"
#include "mbed_events.h"
#include "LoRaWANInterface.h"
#include "Sht31.h"
#include "SX1276_LoRaRadio.h"
#include "C12832.h"

C12832 lcd(SPI_MOSI, SPI_SCK, SPI_MISO, p8, p11);


// Device credentials, register device as OTAA in The Things Network and copy credentials here
static uint8_t DEV_EUI[] = { 0x00, 0xDF, 0x03, 0xB1, 0x74, 0x3A, 0x1C, 0xBB };
static uint8_t APP_EUI[] = { 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x04, 0x0F, 0x91 };
static uint8_t APP_KEY[] = { 0xBF, 0xC3, 0x23, 0xBA, 0x49, 0x94, 0x30, 0xB3, 0x95, 0x5A, 0x8D, 0x6E, 0xF9, 0x7B, 0x1C, 0x8B };

PwmOut speaker(p21);
InterruptIn btn(p12);
int remain_seats=40;

// The port we're sending and receiving on
#define MBED_CONF_LORA_APP_PORT     15

// Peripherals (LoRa radio, temperature sensor and button)
SX1276_LoRaRadio radio(D11, D12, D13, D10, A0, D2, D3, D4, D5, D8, D9, NC, NC, NC, NC, A4, NC, NC);
Sht31 sht31(I2C_SDA, I2C_SCL);

// EventQueue is required to dispatch events around
static EventQueue ev_queue;

// Constructing Mbed LoRaWANInterface and passing it down the radio object.
static LoRaWANInterface lorawan(radio);

// Application specific callbacks
static lorawan_app_callbacks_t callbacks;

// LoRaWAN stack event handler
static void lora_event_handler(lorawan_event_t event);

void play_tone(float frequency, float volume, int interval, int rest) {
    speaker.period(1.0 / frequency);
    speaker = volume;
    wait(interval);
    speaker = 0.0;
    wait(rest);
}

static void pay_the_bill() {
    uint8_t tx_buffer[50] = { 0 };
    // Sending strings over LoRaWAN is not recommended
    sprintf((char*) tx_buffer, "Payment finished");

    int packet_len = strlen((char*) tx_buffer);

    printf("Sending %d bytes: \"%s\"\n", packet_len, tx_buffer);

    int16_t retcode = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_buffer, packet_len, MSG_UNCONFIRMED_FLAG);

    // for some reason send() returns -1... I cannot find out why, the stack returns the right number. I feel that this is some weird Emscripten quirk
    if (retcode < 0) {
        retcode == LORAWAN_STATUS_WOULD_BLOCK ? printf("send - duty cycle violation\n")
                : printf("send() - Error code %d\n", retcode);
        return;
    }

    printf("%d bytes scheduled for transmission\n", retcode);
    remain_seats=remain_seats+1;
    int i=6;
    lcd.cls();
    lcd.locate(5, i);
    lcd.printf("The remaining seats:");
    lcd.locate(5, i + 12);
    char buf[10];
    sprintf(buf, "%d", remain_seats);
    lcd.printf(buf);
    lcd.copy_to_lcd();
    lcd.locate(5, i);
    play_tone(200.0, 0.2, 1, 0);
    play_tone(400.0, 0.5, 1, 2);

}
// Send a message over LoRaWAN
static void send_temperature() {
    uint8_t tx_buffer[50] = { 0 };

    // Sending strings over LoRaWAN is not recommended
    sprintf((char*) tx_buffer, "%3.1f",sht31.readTemperature());

    int packet_len = strlen((char*) tx_buffer);

    printf("Sending %d bytes: \"%s\"\n", packet_len, tx_buffer);

    int16_t retcode = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_buffer, packet_len, MSG_UNCONFIRMED_FLAG);

    // for some reason send() returns -1... I cannot find out why, the stack returns the right number. I feel that this is some weird Emscripten quirk
    if (retcode < 0) {
        retcode == LORAWAN_STATUS_WOULD_BLOCK ? printf("send - duty cycle violation\n")
                : printf("send() - Error code %d\n", retcode);
        return;
    }

    printf("%d bytes scheduled for transmission\n", retcode);
}

static void recieve() {
    uint8_t tx_buffer[50] = { 0 };
    // Sending strings over LoRaWAN is not recommended
    sprintf((char*) tx_buffer, "%3.1f",10);
    int packet_len = strlen((char*) tx_buffer);
    int16_t retcode = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_buffer, packet_len, MSG_UNCONFIRMED_FLAG);

}


// This is called from RX_DONE, so whenever a message came in
static void receive_message()
{
    uint8_t rx_buffer[50] = { 0 };
    int16_t retcode;
    retcode = lorawan.receive(MBED_CONF_LORA_APP_PORT, rx_buffer,
                              sizeof(rx_buffer),
                              MSG_CONFIRMED_FLAG|MSG_UNCONFIRMED_FLAG);

    if (retcode < 0) {
        printf("receive() - Error code %d\n", retcode);
        return;
    }

    printf("Data received on port %d (length %d): ", MBED_CONF_LORA_APP_PORT, retcode);
    for (uint8_t i = 0; i < retcode; i++) {
        printf("%02x ", rx_buffer[i]);
    }
    
    int rx_int;
    rx_int=(int) rx_buffer[0];
    printf("\n");
    if (rx_int==1) {
        lcd.cls();
        int j=6;
        lcd.locate(5, j);
        lcd.printf("Try to measure again");
        lcd.locate(5, j+12);
        lcd.printf("Or go to the hospital ");
        lcd.copy_to_lcd();
        lcd.locate(5, j);
    }
    else if (rx_int==2){
        lcd.cls();
        int j=6;
        lcd.locate(5, j);
        lcd.printf("Sorry, no seat left");
        lcd.copy_to_lcd();
        lcd.locate(5, j);
    }
}

int main() {
    if (DEV_EUI[0] == 0x0 && DEV_EUI[1] == 0x0 && DEV_EUI[2] == 0x0 && DEV_EUI[3] == 0x0 && DEV_EUI[4] == 0x0 && DEV_EUI[5] == 0x0 && DEV_EUI[6] == 0x0 && DEV_EUI[7] == 0x0) {
        printf("Set your LoRaWAN credentials first!\n");
        return -1;
    }

// Enable trace output for this demo, so we can see what the LoRaWAN stack does
    mbed_trace_init();

    if (lorawan.initialize(&ev_queue) != LORAWAN_STATUS_OK) {
        printf("LoRa initialization failed!\n");
        return -1;
    }
    
    
    
    // prepare application callbacks
    callbacks.events = mbed::callback(lora_event_handler);
    lorawan.add_app_callbacks(&callbacks);

    // Disable adaptive data rating
    if (lorawan.disable_adaptive_datarate() != LORAWAN_STATUS_OK) {
        printf("disable_adaptive_datarate failed!\n");
        return -1;
    }

    lorawan.set_datarate(5); // SF7BW125

    lorawan_connect_t connect_params;
    connect_params.connect_type = LORAWAN_CONNECTION_OTAA;
    connect_params.connection_u.otaa.dev_eui = DEV_EUI;
    connect_params.connection_u.otaa.app_eui = APP_EUI;
    connect_params.connection_u.otaa.app_key = APP_KEY;
    connect_params.connection_u.otaa.nb_trials = 3;

    lorawan_status_t retcode = lorawan.connect(connect_params);

    if (retcode == LORAWAN_STATUS_OK ||
        retcode == LORAWAN_STATUS_CONNECT_IN_PROGRESS) {
    } else {
        printf("Connection error, code = %d\n", retcode);
        return -1;
    }

        printf("Connection - In Progress ...\r\n");

        // make your event queue dispatching events forever

    while (1){
        if (remain_seats>0){
            if (sht31.readTemperature()>=36 && sht31.readTemperature()<=37.3)
                remain_seats=remain_seats-1;
                
            btn.fall(ev_queue.event(&pay_the_bill));
            int i=6;
            lcd.cls();
            lcd.locate(5, i);
            lcd.printf("The remaining seats:");
            lcd.locate(5, i + 12);
            char buf[10];
            sprintf(buf, "%d", remain_seats);
            lcd.printf(buf);
            lcd.copy_to_lcd();
            lcd.locate(5, i);
            ev_queue.dispatch(25000);
            send_temperature();
        }
        else{
            ev_queue.dispatch(20000);
            recieve();
        }

    }
    return 0;
}


// Event handler
static void lora_event_handler(lorawan_event_t event) {
    switch (event) {
        case CONNECTED:
            printf("Connection - Successful\n");
            break;
        case DISCONNECTED:
            ev_queue.break_dispatch();
            printf("Disconnected Successfully\n");
            break;
        case TX_DONE:
            printf("Message Sent to Network Server\n");
            break;
        case TX_TIMEOUT:
        case TX_ERROR:
        case TX_CRYPTO_ERROR:
        case TX_SCHEDULING_ERROR:
            printf("Transmission Error - EventCode = %d\n", event);
            break;
        case RX_DONE:
            printf("Received message from Network Server\n");
            receive_message();
            break;
        case RX_TIMEOUT:
        case RX_ERROR:
            printf("Error in reception - Code = %d\n", event);
            break;
        case JOIN_FAILURE:
            printf("OTAA Failed - Check Keys\n");
            break;
        default:
            MBED_ASSERT("Unknown Event");
    }
}

