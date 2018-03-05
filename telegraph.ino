#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <Servo.h>
#include <Stepper.h>
#include "chars.h"

//Пины для для подключения управляемой механики
#define SERVO_PIN 2
#define DOT_PIN 0
#define STEPPER1_PIN 16
#define STEPPER2_PIN 14
#define STEPPER3_PIN 12
#define STEPPER4_PIN 13

//Задержки для электромагнита, в миллисекундах
#define DOT_UP_DELAY 65
#define DOT_DOWN_DELAY 25

//Крайние положения области рисования сервопривода
#define SERVO_MIN 5
#define SERVO_MAX 55

//Количество точек в высоту. Чем больше, тем меньше получаются буквы
//Не должно превышать SERVO_MAX-SERVO_MIN
#define SERVO_STEPS 25

#define SERVO_STEP ( ( SERVO_MAX - SERVO_MIN) / SERVO_STEPS )

//Время, за которое сервопривод перемещается от точки к точке в миллисекундах
#define SERVO_DELAY ( SERVO_STEP * 20 )

//Отступ от нижнего края в точках до текстовой строки
#define LINE_TAB 8

// Дебаг
#define DBG_OUTPUT_PORT Serial

// Настройки wifi
#define WIFI_SSID "Home"
#define WIFI_PASSWORD "deYs453$je23Q"
#define MDNS_HOST "telegraph"

// OTA
#define OTA_PORT 8266
#define OTA_HOSTNAME "telegraph"
#define OTA_PASSWORD "admin"

// Сервопривод
Servo servo;
// Шаговый двигатель
Stepper stepper( 4096, STEPPER1_PIN, STEPPER2_PIN, STEPPER3_PIN, STEPPER4_PIN );
// Сервер
ESP8266WebServer server( 80 );
// OTA
WiFiServer TelnetServer( 8266 );

/**
 * Возвращает MIME типа файла.
 */
String getContentType( String filename )
{
  if( server.hasArg( "download" ) )
    return "application/octet-stream";
  else if( filename.endsWith( ".htm" ) )
    return "text/html";
  else if( filename.endsWith( ".html" ) )
    return "text/html";
  else if( filename.endsWith( ".css" ) )
    return "text/css";
  else if( filename.endsWith( ".js" ) )
    return "application/javascript";
  else if( filename.endsWith( ".png" ) )
    return "image/png";
  else if( filename.endsWith( ".gif" ) )
    return "image/gif";
  else if( filename.endsWith( ".jpg" ) )
    return "image/jpeg";
  else if( filename.endsWith( ".ico" ) )
    return "image/x-icon";
  else if( filename.endsWith( ".gz" ) )
    return "application/x-gzip";

  return "text/plain";
}

/**
 * Обработка запроса файла.
 */
bool handleFileRead( String path )
{
  DBG_OUTPUT_PORT.println( "handleFileRead: " + path );
  if( path.endsWith( "/" ) )
    path += "index.html";

  String contentType = getContentType( path );
  String pathWithGz = path + ".gz";
  if( SPIFFS.exists( pathWithGz ) || SPIFFS.exists( path ) )
  {
    if( SPIFFS.exists( pathWithGz ) )
      path += ".gz";
    File file = SPIFFS.open( path, "r" );
    size_t sent = server.streamFile( file, contentType );
    file.close();
    return true;
  }

  return false;
}

/**
 * Обработка нового сообщения на печать.
 */
void handleMessage()
{
  if( !server.hasArg( "message" ) )
  {
    server.send( 500, "text/plain", "BAD ARGS" );
    return;
  }

  String message = server.arg( "message" );
  DBG_OUTPUT_PORT.println( "handleMessage: " + message );

  server.send( 200, "text/json; charset=utf-8", "{\"message\": \"" + message + "\", \"status\": \"ok\"}" );
  printString( message  );
}

/**
 * Функция для рисования точки на бумаге.
 */
void ping() {
  digitalWrite(DOT_PIN, 1);
  delay(DOT_UP_DELAY);
  digitalWrite(DOT_PIN, 0);
  delay(DOT_DOWN_DELAY);
}

//функция для рисования столбца из 8 точек по заданному байту, 
//в котором каждый бит понимается как точка
void printLine(int b) {
  //отъезжаем в начало столбца
  servo.write(SERVO_MAX - (LINE_TAB)*SERVO_STEP);

  //Если столбец не пустой, то мы его рисуем,
  //В противном случае, просто протягиваем ленту
  if(b != 0) {
    servo.write(SERVO_MAX - (LINE_TAB-1)*SERVO_STEP);
    delay(SERVO_DELAY*12);

    //Последовательно печатаем точки считывая информацию бит за битом
    for (int j = 0; b != 0; j++) {
      servo.write(SERVO_MAX - (LINE_TAB+j)*SERVO_STEP);
      delay(SERVO_DELAY);
  
      if(b & 1) ping();
      b >>= 1;
    }
  }

  // Расстояние между линиями
  DBG_OUTPUT_PORT.println( "Run stepper" );
  stepper.step( 24 );
}

//Функция печатает символ в соответствии с установленным шрифтом,
//Вызывая последовательно функции printLine()
void printChar(char c) {
  DBG_OUTPUT_PORT.print( "Print " );
  DBG_OUTPUT_PORT.println( c );

  for( int i = 0; i < 8; i++ )
  {
    if( chars[ c ][ i ] != 0 )
    {
      printLine( chars[ c ][ i ] );
    }
    else
    {
      // Расстояние между буквами
      DBG_OUTPUT_PORT.println( "Run stepper" );
      stepper.step( 15 );
    }
  }
}

//Фукция печатает поступающую ей на вход строку,
//Вызывая последовательно функции printChar()
void printString( String str )
{
  char c;
  char last;
  byte i = 0;
  DBG_OUTPUT_PORT.print( "Will print " );
  DBG_OUTPUT_PORT.println( str );

  if( !servo.attached() )
  {
    DBG_OUTPUT_PORT.println( "Attach servo" );
    servo.attach( SERVO_PIN );
  }

  while( i < str.length() )
  {
    // Вроде русский, нам нужен следующий байт а этот пропускаем
    if( str.charAt( i ) == 0xD0 || str.charAt( i ) == 0xD1 )
    {
      last = str.charAt( i );
      i++;
      continue;
    }

    // Тут нужный символ для печати, русские сами направляем, прочие как есть
    switch( str.charAt( i ) )
    {
      case 0x90: // А
      case 0xB0: // а
        c = 0;
        break;
      case 0xB1: // б
        c = 1;
        break;
      case 0x92: // В
      case 0xB2: // в
        c = 2;
        break;
      case 0x93: // Г
      case 0xB3: // г
        c = 3;
        break;
      case 0x94: // Д
      case 0xB4: // д
        c = 4;
        break;
      case 0x95: // Е
      case 0xB5: // е
      case 0x81: // Ё или с
      case 0x91: // ё или Б
        if( last == 0xD1 && str.charAt( i ) == 0x81 ) // Это с
          c = 17;
        else if( last == 0xD0 && str.charAt( i ) == 0x91 ) // Это Б
          c = 1;
        else // Это какая-то ё или е, одно и то же
          c = 5;
        break;
      case 0x96: // Ж
      case 0xB6: // ж
        c = 6;
        break;
      case 0x97: // З
      case 0xB7: // з
        c = 7;
        break;
      case 0x98: // И
      case 0xB8: // И
        c = 8;
        break;
      case 0x99: // Й
      case 0xB9: // й
        c = 9;
        break;
      case 0x9A: // К
      case 0xBA: // к
        c = 10;
        break;
      case 0x9B: // Л
      case 0xBB: // л
        c = 11;
        break;
      case 0x9C: // М
      case 0xBC: // м
        c = 12;
        break;
      case 0x9D: // Н
      case 0xBD: // н
        c = 13;
        break;
      case 0x9E: // О
      case 0xBE: // о
        c = 14;
        break;
      case 0x9F: // П
      case 0xBF: // п
        c = 15;
        break;
      case 0xA0: // Р
      case 0x80: // р
        c = 16;
        break;
      case 0xA1: // С
        c = 17;
        break;
      case 0xA2: // Т
      case 0x82: // т
        c = 18;
        break;
      case 0xA3: // У
      case 0x83: // у
        c = 19;
        break;
      case 0xA4: // Ф
      case 0x84: // ф
        c = 20;
        break;
      case 0xA5: // Х
      case 0x85: // х
        c = 21;
        break;
      case 0xA6: // Ц
      case 0x86: // ц
        c = 22;
        break;
      case 0xA7: // Ч
      case 0x87: // ч
        c = 23;
        break;
      case 0xA8: // Ш
      case 0x88: // Ш
        c = 24;
        break;
      case 0xA9: // Щ
      case 0x89: // щ
        c = 25;
        break;
      case 0xAA: // Ъ
      case 0x8A: // ъ
        c = 26;
        break;
      case 0xAB: // Ы
      case 0x8B: // ы
        c = 27;
        break;
      case 0xAC: // Ь
      case 0x8C: // ь
        c = 28;
        break;
      case 0xAD: // Э
      case 0x8D: // э
        c = 29;
        break;
      case 0xAE: // Ю
      case 0x8E: // ю
        c = 30;
        break;
      case 0xAF: // Я
      case 0x8F: // я
        c = 31;
        break;
      default:
        c = str.charAt( i );
        break;
    }

    printChar( c );
    last = 0;
    i++;
  }

  DBG_OUTPUT_PORT.println( "Detach servo" );
  servo.detach();
  // Конец печатаемой фразы, протаскивание
  stepper.step( 100 );
}

/**
 * Инициалзация.
 */
void setup()
{
  // Инициализация дебага
  DBG_OUTPUT_PORT.begin( 115200 );
  DBG_OUTPUT_PORT.print( "\n" );
  DBG_OUTPUT_PORT.setDebugOutput( true );
  SPIFFS.begin();

  // Чтобы Arduino IDE детектировало порт для OTA
  TelnetServer.begin();

  // Подключение к wifi
  DBG_OUTPUT_PORT.printf( "Connecting to %s\n", WIFI_SSID );
  if( String( WiFi.SSID() ) != String( WIFI_SSID ) )
  {
    WiFi.begin( WIFI_SSID, WIFI_PASSWORD );
  }
  while( WiFi.status() != WL_CONNECTED )
  {
    delay( 500 );
    DBG_OUTPUT_PORT.print( "." );
  }
  DBG_OUTPUT_PORT.println( "" );
  DBG_OUTPUT_PORT.print( "Connected! IP address: " );
  DBG_OUTPUT_PORT.println( WiFi.localIP() );

  // mdns
  MDNS.begin( MDNS_HOST );
  DBG_OUTPUT_PORT.print( "mdns: http://" );
  DBG_OUTPUT_PORT.print( MDNS_HOST );
  DBG_OUTPUT_PORT.println( ".local" );

  // Инициализация сервера
  // Новое сообщение на печать
  server.on( "/message", HTTP_POST, handleMessage );
  // Файл не найден
  server.onNotFound( [] () {
    // Сперва проверяем есть ли он в памяти, если нет то уже кидаем 404
    if( !handleFileRead( server.uri() ) )
      server.send( 404, "text/plain", "FileNotFound" );
  });
  server.begin();
  DBG_OUTPUT_PORT.println( "HTTP server started" );

  // OTA
  ArduinoOTA.setPort( OTA_PORT );
  ArduinoOTA.setHostname( OTA_HOSTNAME );
//  ArduinoOTA.setPassword( OTA_PASSWORD );
  ArduinoOTA.onStart([]() {
    DBG_OUTPUT_PORT.println("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    DBG_OUTPUT_PORT.println("OTA End");
    DBG_OUTPUT_PORT.println("Rebooting...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DBG_OUTPUT_PORT.printf("Progress: %u%%\r\n", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    DBG_OUTPUT_PORT.printf("Error[%u]: ", error);
    if(error == OTA_AUTH_ERROR)
      DBG_OUTPUT_PORT.println("Auth Failed");
    else if(error == OTA_BEGIN_ERROR)
      DBG_OUTPUT_PORT.println("Begin Failed");
    else if(error == OTA_CONNECT_ERROR)
      DBG_OUTPUT_PORT.println("Connect Failed");
    else if(error == OTA_RECEIVE_ERROR)
      DBG_OUTPUT_PORT.println("Receive Failed");
    else if(error == OTA_END_ERROR)
      DBG_OUTPUT_PORT.println("End Failed");
  });
  ArduinoOTA.begin();

  // Инициализация ленты
  pinMode( DOT_PIN, OUTPUT );
  stepper.setSpeed( 3 );
  stepper.step( 30 );

  printString( String( "Hello" ) );
}

/**
 * Рабочий цикл.
 */
void loop()
{
  server.handleClient();
  ArduinoOTA.handle();
}

