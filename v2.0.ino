#include <SPI.h>
#include <MFRC522.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>

#define BUZZER_PIN 5
#define SS_PIN 10
#define RST_PIN 9
#define LED_VERDE_PIN 6
#define LED_VERMELHO_PIN 7
#define TRAVA_PIN 8  // Porta conectada à trava solenoide
#define RX_PIN 3
#define TX_PIN 2

MFRC522 mfrc522(SS_PIN, RST_PIN);
SoftwareSerial BTSerial(RX_PIN, TX_PIN);
RTC_DS3231 rtc;

struct LogEntry {
  byte uid[4];
  DateTime time;
  bool liberada;  // Novo membro para o estado da tranca
};

const int numTagsMax = 10;
const int logCapacity = 10; // Capacidade máxima de registros do log
char command;
bool modoCadastro = false;
bool modoDescadastro = false;
byte tagsAutorizadas[numTagsMax][4];

void setup() {
  Serial.begin(9600);
  BTSerial.begin(9600);
  SPI.begin();
  mfrc522.PCD_Init();
  Wire.begin();
  pinMode(BUZZER_PIN, OUTPUT);
  if (!rtc.begin()) {
    Serial.println("Não foi possível encontrar o RTC.");
    while (1);
  }
  if (rtc.lostPower()) {
    Serial.println("RTC perdeu energia, configurando data e hora padrão.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  pinMode(LED_VERDE_PIN, OUTPUT);
  pinMode(LED_VERMELHO_PIN, OUTPUT);
  pinMode(TRAVA_PIN, OUTPUT);
  digitalWrite(LED_VERDE_PIN, LOW);
  digitalWrite(LED_VERMELHO_PIN, LOW);
  digitalWrite(TRAVA_PIN, HIGH); // Trava inicialmente fechada (LOW = fechada)
  for (int i = 0; i < numTagsMax; i++) {
    for (int j = 0; j < 4; j++) {
      tagsAutorizadas[i][j] = 0xFF;
    }
  }
  Serial.println("Sistema pronto. Aproxime o cartão...");
  Serial.println("Esperando comando via Bluetooth...");
}

void loop() {
  // Verifica se há um comando disponível no Serial Bluetooth
  if (BTSerial.available()) {
    command = BTSerial.read();
    Serial.print("Comando recebido: ");
    Serial.println(command);
    if (command == 'C') {
      modoCadastro = true;
      Serial.println("Entrando em modo cadastro. Aproxime uma tag para cadastrar.");
      while (modoCadastro) {
        digitalWrite(LED_VERDE_PIN, HIGH);
        digitalWrite(LED_VERMELHO_PIN, LOW);
        delay(500);
        digitalWrite(LED_VERDE_PIN, LOW);
        digitalWrite(LED_VERMELHO_PIN, HIGH);
        delay(500);
        if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
          cadastrarTag();
          modoCadastro = false;
          digitalWrite(LED_VERDE_PIN, LOW);
          digitalWrite(LED_VERMELHO_PIN, LOW);
        }
        if (BTSerial.available() && BTSerial.read() == 'F') {
          modoCadastro = false;
          digitalWrite(LED_VERDE_PIN, LOW);
          digitalWrite(LED_VERMELHO_PIN, LOW);
          Serial.println("Saindo do modo cadastro.");
        }
      }
    } else if (command == 'X') {
      modoDescadastro = true;
      Serial.println("Entrando em modo excluir tag. Aproxime uma tag para excluir.");
      while (modoDescadastro) {
        digitalWrite(LED_VERMELHO_PIN, HIGH);
        delay(100);
        digitalWrite(LED_VERMELHO_PIN, LOW);
        delay(100);
        if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
          descadastrarTag();
          modoDescadastro = false;
          digitalWrite(LED_VERDE_PIN, LOW);
          digitalWrite(LED_VERMELHO_PIN, LOW);
        }
        if (BTSerial.available() && BTSerial.read() == 'F') {
          modoDescadastro = false;
          digitalWrite(LED_VERDE_PIN, LOW);
          digitalWrite(LED_VERMELHO_PIN, LOW);
          Serial.println("Saindo do modo excluir tag.");
        }
      }
    } else if (command == 'L') {
      Serial.println("Iniciando envio de log...");
      enviarLog();
    }
  }
  
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    DateTime now = rtc.now();
    Serial.print("UID do cartão: ");
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
      Serial.print(mfrc522.uid.uidByte[i], HEX);
    }
    Serial.print(" - Data e Hora: ");
    Serial.print(now.day()); Serial.print('/');
    Serial.print(now.month()); Serial.print('/');
    Serial.print(now.year()); Serial.print(" ");
    Serial.print(now.hour()); Serial.print(':');
    Serial.println(now.minute());
    if (tagAutorizada(mfrc522.uid.uidByte)) {
      digitalWrite(LED_VERDE_PIN, HIGH);
      digitalWrite(LED_VERMELHO_PIN, LOW);
      digitalWrite(TRAVA_PIN, LOW); // Abre a trava (LOW = liberada)
      Serial.println("Tag autorizada. Trava liberada.");
      buzzerSuccess();
      delay(3000);  // Aguarda 3 segundos
      digitalWrite(LED_VERDE_PIN, LOW);
      digitalWrite(TRAVA_PIN, HIGH);  // Fecha a trava após o tempo (HIGH = fechada)
      Serial.println("Trava fechada.");
      registrarLog(mfrc522.uid.uidByte, now, true);
    } else {
      digitalWrite(LED_VERDE_PIN, LOW);
      digitalWrite(LED_VERMELHO_PIN, HIGH);
      Serial.println("Tag não autorizada.");
      buzzerFailure();
      registrarLog(mfrc522.uid.uidByte, now, false);
    }
    exibirTagsCadastradas();
    delay(1500);
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_VERMELHO_PIN, LOW);
  }
}

bool tagAutorizada(byte *uid) {
  for (int i = 0; i < numTagsMax; i++) {
    if (memcmp(uid, tagsAutorizadas[i], 4) == 0) {
      return true;
    }
  }
  return false;
}

void cadastrarTag() {
  byte uid[4];
  for (byte i = 0; i < 4; i++) {
    uid[i] = mfrc522.uid.uidByte[i];
  }

  // Verifica se a tag já está cadastrada
  for (int i = 0; i < numTagsMax; i++) {
    if (memcmp(uid, tagsAutorizadas[i], 4) == 0) {
      Serial.println("Tag já cadastrada.");
      buzzerFailure();
      return;
    }
  }

  // Procura uma posição vazia para cadastrar a tag
  for (int i = 0; i < numTagsMax; i++) {
    if (tagsAutorizadas[i][0] == 0xFF) {
      memcpy(tagsAutorizadas[i], uid, 4);
      Serial.println("Tag cadastrada com sucesso!");
      buzzerCadastroConfirmado();
      return;
    }
  }
  Serial.println("Limite de tags cadastradas atingido.");
}

void descadastrarTag() {
  byte uid[4];
  for (byte i = 0; i < 4; i++) {
    uid[i] = mfrc522.uid.uidByte[i];
  }
  for (int i = 0; i < numTagsMax; i++) {
    if (memcmp(uid, tagsAutorizadas[i], 4) == 0) {
      for (byte j = 0; j < 4; j++) {
        tagsAutorizadas[i][j] = 0xFF;
      }
      Serial.println("Tag descadastrada com sucesso!");
      return;
    }
  }
  Serial.println("Tag não encontrada.");
}

void registrarLog(byte *uid, DateTime now, bool liberada) {
  LogEntry entry;
  for (byte i = 0; i < 4; i++) {
    entry.uid[i] = uid[i];
  }
  entry.time = now;
  entry.liberada = liberada; // Adiciona o estado da tranca ao registro
  int logPos = EEPROM.read(0);
  logPos = (logPos + 1) % logCapacity; // Controle circular do log
  EEPROM.write(0, logPos);
  EEPROM.put(logPos * sizeof(LogEntry) + 1, entry);
}

void enviarLog() {
  Serial.println("Enviando log...");
  int logPos = EEPROM.read(0);
  for (int i = 0; i < logCapacity; i++) {
    int pos = (logPos - i + logCapacity) % logCapacity; // Lê os registros do mais recente ao mais antigo
    LogEntry entry;
    EEPROM.get(pos * sizeof(LogEntry) + 1, entry);
    if (entry.time.unixtime() > 0) { // Garante que só registros válidos serão enviados
      String logEntry = "UID: ";
      for (byte j = 0; j < 4; j++) {
        logEntry += String(entry.uid[j], HEX);
      }
      logEntry += " Data: ";
      logEntry += String(entry.time.day()) + "/";
      logEntry += String(entry.time.month()) + "/";
      logEntry += String(entry.time.year());
      logEntry += " Hora: ";
      logEntry += String(entry.time.hour()) + ":";
      logEntry += String(entry.time.minute());
      logEntry += " Tranca: ";
      logEntry += entry.liberada ? "Liberada" : "Não Liberada";
      
      // Envia o log via Bluetooth
      BTSerial.println(logEntry);
      Serial.println(logEntry); // Também imprime no Serial Monitor para verificação
    }
  }
}

void exibirTagsCadastradas() {
  Serial.println("Tags cadastradas:");
  for (int i = 0; i < numTagsMax; i++) {
    Serial.print("Tag "); Serial.print(i + 1); Serial.print(": ");
    if (tagsAutorizadas[i][0] != 0xFF) {
      for (byte j = 0; j < 4; j++) {
        Serial.print(tagsAutorizadas[i][j] < 0x10 ? " 0" : " ");
        Serial.print(tagsAutorizadas[i][j], HEX);
      }
    } else {
      Serial.print("Vazio");
    }
    Serial.println();
  }
}

void buzzerSuccess() {
  for (int i = 0; i < 2; i++) {
    tone(BUZZER_PIN, 3000, 200);
    delay(300);
  }
}

void buzzerFailure() {
  tone(BUZZER_PIN, 3000, 1000);
}

void buzzerCadastroConfirmado() {
  tone(BUZZER_PIN, 3000, 500);
}

