#include <Arduino.h>
#include <cJSON.h>
#include <WiFi.h>
#include <Web3.h>
#include <Util.h>
#include <Contract.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "../include/ca_cert.h"
#define SSID "" //Wifi SSID
#define PASSW "" //WiFi passwd
#define INFURA_HOST "kovan.infura.io"
#define INFURA_PATH "/v3/..." //Endpoint for Infura's Ethereum API 
#define PRIVATE_KEY "" //Private key for the address from which the temperature will be sent
#define ETHERSCAN_TX "https://kovan.etherscan.io/tx/" 
const string espAccountAdress = "";
Web3 web3(INFURA_HOST, INFURA_PATH);

//GPIO pin used for the oneWireBus
const int oneWireBus = 26;     
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

//Converts a string to a hexString
string string_to_hex(const string *str){
	string ret="";
	char buffer[2];
	for(int i = 0; i < str->length();i++){
		itoa((*str)[i], buffer, 16);
		ret.append(buffer);
	}
	return ret;
}
//gets the temparature
float getTemp(){
	sensors.requestTemperatures(); 
	return sensors.getTempCByIndex(0);
}

void setupWifi(){
  	WiFi.begin(SSID, PASSW);

	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.println("Connecting to WiFi..");
	}
}
//scans the last 5 blocks of the blockchain 
//for transactions that are sent to the esp32 address
vector<string> getTransactions(){
	int lastBlockNumber = web3.EthBlockNumber();
	vector<string> addresses;
	WiFiClientSecure client;
	client.setCACert(ca_cert);
	int connected = client.connect(INFURA_HOST, 443);
	delay(50);
    	if (!connected) {
		Serial.print("Unable to connect to infura api.");
		return addresses;
    	}
	for (int blockNumber = lastBlockNumber; blockNumber > lastBlockNumber-5; blockNumber-- ){
		Serial.print("Resolving blokc number: ");
		Serial.println(blockNumber);
		string m = "eth_getBlockByNumber";
		stringstream blockNumberHex;
		blockNumberHex <<hex<<blockNumber;
		string p = "[\"0x"+string(blockNumberHex.str().c_str())+"\", true]";
		string input = "{\"jsonrpc\":\"2.0\",\"method\":\"" + m + "\",\"params\":" + p + ",\"id\":1}";
		int l = input.size();
		stringstream ss;
		ss << l;
		string lstr = ss.str();

		client.println(("POST " + string(INFURA_PATH)  + " HTTP/1.1").c_str());
		client.println(("HOST: " + string(INFURA_HOST)).c_str());
		client.println("Content-Type: application/json");
		client.println(("Content-Length: " + lstr).c_str());
		client.println();
		client.println(input.c_str());
		//preskoći HTTP zaglavlje
		while (client.connected()) {
			String line = client.readStringUntil('\n');
			if (line == "\r") {
				break;
			}
		}
		if (client.available()){
			char word[20];
			do {
				if (strlen(word) > 15){
					strcpy(word, "");
				}
				char buffer;
				client.readBytes(&buffer, 1);
				if (buffer == ','){
					strcpy(word, "");
					continue;
				}
				strncat(word, &buffer, 1);				
			}while(strcmp(word,"\"transactions\"")!=0);
			strcpy(word, "");
			bool end = false;
			do {
				//read one property
				client.readStringUntil('{'); //find beginning
				char to[43]; strcpy(to, ""); 
				char from[43]; strcpy(from, "");
				byte prazan = 0;
				while(1337){
					strcpy(word, client.readStringUntil(':').c_str());
					if(strcmp(word, "") == 0){
						prazan++;
						if (prazan>1){
							end = true;
							break;
						}
					}
					if(strcmp(word,"\"to\"") == 0){
						client.readStringUntil('"');
						strcpy(to, client.readStringUntil('"').c_str());
						client.readStringUntil(',');
					}else if(strcmp(word, "\"from\"")==0){
						client.readStringUntil('"');
						strcpy(from, client.readStringUntil('"').c_str());
						client.readStringUntil(',');
					}
					else{
						strcpy(word, "");
						String str = client.readStringUntil(',');
						if(str.endsWith("}")){
							break;
						}
						else if(str.endsWith("}]")){
							end = true;
							break;
						}
					}
				}
				Serial.print("From: ");
				Serial.println(from);
				Serial.print("To: ");
				Serial.println(to);
				
				if (strcmp(to, espAccountAdress.c_str()) == 0 && strcmp(from, "") != 0){
					addresses.push_back(from);
				}			
			} while(!end);
		}
	}
	client.stop();
	return addresses;

}
void resolve(string *address, double eth){
	Serial.println("Setting up contract");
	Contract contract(&web3, "");

	Serial.println("Setting up private key");
	contract.SetPrivateKey(PRIVATE_KEY);
	unsigned long long gasPriceVal = 22000000000ULL;
	uint256_t weiValue = Util::ConvertToWei(eth, 18);
	uint32_t  gasLimitVal = 90000;

	Serial.println("GettingNonce");
	uint32_t nonceVal = (uint32_t)web3.EthGetTransactionCount(&espAccountAdress);
	string tempValue;
	string data; 
	uint256_t money = web3.EthGetBalance(&espAccountAdress);
	string moneyStr = Util::ConvertWeiToEthString(&money,6);
	Serial.println("Amount of ETH:");
	Serial.println(moneyStr.c_str());
	Serial.println("Getting temperature");
	tempValue = "Temperature is "+ string(String(getTemp()).c_str()) + "C"; // dumb bit ok
	Serial.println(tempValue.c_str());
	data = string_to_hex(&tempValue);
	Serial.println(data.c_str());
	Serial.println("Sending request");
	string result = contract.SendTransaction(nonceVal, gasPriceVal, gasLimitVal, address, &weiValue, &data);
	Serial.println(result.c_str());
	string transactionHash = web3.getString(&result);
	Serial.println("TX on Etherscan:");
	Serial.print(ETHERSCAN_TX);
	Serial.println(transactionHash.c_str()); //you can go straight to etherscan and see the pending transaction
}	

void setup(){
	Serial.begin(9600);
	setupWifi();
	sensors.begin();
	delay(1000);
	Serial.println(getTemp());

}

void loop() {
	vector<string> addresses = getTransactions();
	
	Serial.print("Found:");
	Serial.println(addresses.size());
	for(short int i = 0; i < addresses.size(); i++ ){
		resolve(&addresses[i], 0.000000005);
	}

	addresses.clear();
	delay(20000); 
}