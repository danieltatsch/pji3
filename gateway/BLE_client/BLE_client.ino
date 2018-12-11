#include "BLEDevice.h"
//#define BACKOFF_TIME 20000
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

uint16_t BACKOFF_TIME = 30000;

hw_timer_t *timer = NULL; //faz o controle do temporizador (interrupção por tempo)
//callback do watchdog

// Serviço remoto que se deseja conectar
static BLEUUID serviceUUID("0000ffe0-0000-1000-8000-00805f9b34fb");
// Característica associada ao serviço que estamos interessados
static BLEUUID    charUUID("0000ffe1-0000-1000-8000-00805f9b34fb");

String BEACONS[2] = {"88:4a:ea:00:5e:07", "c8:fd:19:07:f2:29"};

static BLEAddress *pServerAddress;
static boolean doConnect = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;

struct Disp_BLE{
    BLEClient*  pClient  = BLEDevice::createClient();
//    BLEAddress *pServerAddress;
    BLEAdvertisedDevice advertisedDevice;
    boolean leu = false;
    uint8_t temp;
    uint8_t hum;
    uint8_t temp_id;
    uint8_t hum_id;
}aux;

static uint8_t xablau = 0;
static long int last_time = 0, current_time= 0;
static String last_mac = "", current_mac = "";

void IRAM_ATTR resetModule(){
    ets_printf("WATCHDOG ATIVADO, REINICIANDO...\n"); //imprime no log
    esp_restart_noos(); //reinicia o chip
}

bool connect_wifi(){
    const char* ssid = "rede-jessica";
    const char* password =  "jessicarede";

    WiFi.begin(ssid, password); 
  
    while (WiFi.status() != WL_CONNECTED) { //Check for the connection
      delay(1000);
      Serial.println("Conectando com rede Wi-Fi...");
    }
    if(WiFi.status() == WL_CONNECTED){
      Serial.println("Conectado, enviando dados dos sensores");
      return true;
    }else{
      Serial.println("Erro na conexão!");
      return false;
    }
}

void send_data(int sensor_id, int dado_sensor){  
    StaticJsonBuffer<100> JSONbuffer;   //Declaring static JSON buffer
    JsonObject& JSONencoder = JSONbuffer.createObject(); 


    String mac = WiFi.macAddress();
    String gateway_id = "";
    for (int i = 0; i < mac.length(); i ++){
      if (mac[i] == ':') continue;
      gateway_id += mac[i];
    } 

    Serial.print("MAC_GATEWAY: ");
    Serial.println(gateway_id);
    
    JSONencoder["gateway_id"] = gateway_id;
    JSONencoder["sensor_id"] = sensor_id;
    JSONencoder["dado_sensor"] = dado_sensor;
    JSONencoder["rssi"] = aux.advertisedDevice.getRSSI();
    
    char JSONmessageBuffer[100];
    JSONencoder.prettyPrintTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));

    HTTPClient http;    //Declare object of class HTTPClient
    
    http.begin("http://192.168.137.153:5000/save_data");              //Specify destination for HTTP request
    http.addHeader("Content-Type", "application/json");         //Specify content-type header

    int httpCode = http.POST(JSONmessageBuffer);   //Send the request
    
//    String payload = http.getString();             //Get the response payload
    
//    Serial.println(httpCode);   //Print HTTP return code
//    Serial.println(payload);    //Print request response payload
    http.end();  //Free resources
    
    if (httpCode == 200){
      Serial.println("DADOS ENVIADOS PARA O BANCO!");
    }
    else {
      Serial.println("Erro no envio, ignorando dados.");  
    }
    timerWrite(timer, 0); //reseta o temporizador (alimenta o watchdog) 
}

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    Serial.print("Notificação recebida de: ");
    Serial.println(pBLERemoteCharacteristic->getUUID().toString().c_str());
    
    Serial.print("Umidade: ");
    Serial.print(pData[0]);
    Serial.print(" %\t");
    Serial.print("Temperatura: ");
    Serial.print(pData[1]);
    Serial.println(" *C ");
    aux.hum = pData[0];
    aux.temp = pData[1];
    aux.leu = true;
    timerWrite(timer, 0); //reseta o temporizador (alimenta o watchdog) 
    aux.pClient->disconnect();
    delay(1500);
}

// VERIFICA ULTIMA CONEXAO DO BEACON
bool verifica_conexao(){
    current_mac = aux.advertisedDevice.getAddress().toString().c_str();
    
    if((current_mac.equals(last_mac))){
      long int tempo = current_time - last_time;
      if (tempo < BACKOFF_TIME){ // se houve um novo anuncio do mesmo beacon em menos de BACKOFF_TIME segundos
        Serial.print("MUITO CEDO, IGNORANDO: ");
        Serial.println(aux.advertisedDevice.getName().c_str());
        Serial.print("Tempo em espera: ");
        Serial.print(tempo/1000);
        Serial.println(" segundos");
        xablau = 1;
        return false;
      }else{  // se ja se passaram os BACKOFF_TIME segundos
        last_time = current_time;
        Serial.print("Dispositivo conectado: ");
        Serial.println(aux.advertisedDevice.getName().c_str());
        timerWrite(timer, 0); //reseta o temporizador (alimenta o watchdog) 
        return true;
      }
    }else{ // se o dispositivo que se conectou não é o mesmo que o anterior
        Serial.print("Dispositivo conectado: ");
        Serial.println(aux.advertisedDevice.getName().c_str());
        last_mac = current_mac;
        last_time = current_time;
        timerWrite(timer, 0); //reseta o temporizador (alimenta o watchdog) 
        return true;
     }
}

bool connectToServer(BLEAddress pAddress) {
    if (verifica_conexao()){
        BLEClient*  pClient  = BLEDevice::createClient();
        aux.pClient = pClient;
        pClient->connect(pAddress);
    
        // Obtain a reference to the service we are after in the remote BLE server.
        BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
        if (pRemoteService == nullptr) {
          Serial.print("Falha ao encontrar UUID de servico: ");
          Serial.println(serviceUUID.toString().c_str());
          return false;
        }
        
        // Obtain a reference to the characteristic in the service of the remote BLE server.
        pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
        if (pRemoteCharacteristic == nullptr) {
          Serial.print("Falha ao encotrar UUID da caracteristica: ");
          Serial.println(charUUID.toString().c_str());
          return false;
        }
      
        pRemoteCharacteristic->registerForNotify(notifyCallback);
        return true;
    }else return false;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("Dispositivo BLE Encontrado: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(serviceUUID)) {

      advertisedDevice.getScan()->stop();
      current_time = millis();

      pServerAddress = new BLEAddress(advertisedDevice.getAddress());
      aux.advertisedDevice = advertisedDevice;
      doConnect = true;

    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks


void setup() {
  Serial.begin(230400);
  //---------------------------------------------------------------------------
  //CONFIGURACAO DO WATCHDOG
  //---------------------------------------------------------------------------
  timer = timerBegin(0, 80, true); //timerID 0, div 80
  
  //timer, callback, interrupção de borda
  timerAttachInterrupt(timer, &resetModule, true);
  
  //timer, tempo (us), repetição
  timerAlarmWrite(timer, 30000000, true);
  timerAlarmEnable(timer); //habilita a interrupção 
  Serial.println("Watchdog configurado!");
  delay(100);
  Serial.println("Iniciando Scanner BLE");
  //---------------------------------------------------------------------------
  // CONFIGURACAO DO SCANNER BLE
  //---------------------------------------------------------------------------
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 30 seconds.
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(30);
  //---------------------------------------------------------------------------
} 


// This is the Arduino main loop function.
void loop() {
  
  if (doConnect == true) {
    connectToServer(*pServerAddress);
    doConnect = false;
  }
 
  // inicia um novo scan
  // aqui deve verificar se uma nova informacao foi recebida (USAR AUX.LEU)
  // e a conexao BLE é fehcada, aqui que tera que ser chamada uma
  // funcao para mandar infos via Wi-Fi
    if (aux.leu){
      if (connect_wifi()){
        String mac_atual = aux.advertisedDevice.getAddress().toString().c_str();
        if(mac_atual.equals(BEACONS[0])){
          send_data(4, (int)aux.temp);
          send_data(5, (int)aux.hum);
        }else{
          send_data(6, (int)aux.temp);
          send_data(7, (int)aux.hum);
        }
      }
      WiFi.disconnect();
      aux.leu = false;
      delay(1000);
      Serial.println("Reiniciando scan BLE...");
      delay(500);
      
      BLEScan* pBLEScan = BLEDevice::getScan();
      pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
      pBLEScan->setActiveScan(true);
      pBLEScan->start(30);
    }else if(xablau){
        Serial.println("Reiniciando scan BLE...");
        BLEScan* pBLEScan = BLEDevice::getScan();
        pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
        pBLEScan->setActiveScan(true);
        pBLEScan->start(30);
     }
}
