#include <string.h>

struct AP_Info {
  String SSID_Name, userName, passPhase;
  AP_Info(String sName, String pPhase, String uName=String("")) : SSID_Name(sName), userName(uName), passPhase(pPhase) {}
};

//AP_Info SchoolAP("ChulaWiFi", "CUNetPassword", "StudentID");
AP_Info SchoolAP("ChulaWiFi", "ohm0910292525", "6330388021");
//AP_Info HomeAP("myHomeSSID", "HomePassphase");
AP_Info HomeAP("Rebelruby2G", "ami800833");
//AP_Info MobileAP("myHotspot", "HotspotPassword");
AP_Info MobileAP("Bubble", "bubble1710");

//Netpie broker
const char *mqtt_server = "broker.netpie.io";
const int mqtt_port = 1883;
const char *mqtt_client = "d7e75cb8-026a-4f6a-af9d-ae509ddbc673";  //From NETPIE device key
const char *mqtt_username = "SxpW7XHQ2o4QRofZt7GHMqcxFmMMvdQz";    //From NETPIE device key
const char *mqtt_password = "SiC3sbnRxA(iPMVNBMs7!l#gI!krmi)!";    //From NETPIE device key

String url = "https://ds.netpie.io/feed/api/v1/datapoints/query";