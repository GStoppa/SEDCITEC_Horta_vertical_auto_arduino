/*
 * ============================================================================
 * PROJETO: A Horta de DS - Horta Vertical Automatizada
 * DISCIPLINA: Biologia 2 (SPOBIO2)
 * EVENTO: Exposição Saúde e Alimentação - SEDCITEC
 * INSTITUIÇÃO: Instituto Federal de Educação, Ciência e Tecnologia de São Paulo (IFSP - Campus São Paulo)
 *
 * TURMA:
 * 231-A - 2° ano curso técnico em desenvolvimento de sistemas integrado com o ensino médio (IFSP - Campus São Paulo)
 *
 * AUTOR DO CÓDIGO:
 * Gustavo Vieira Stoppa 
 * 
 * INTEGRANTES (Estruturação da Horta):
 * - Arthur Fukuyama de Andrade
 * - Giovanni Carro
 * - Guilherme Antero Fiuza
 * - Heitor Akira Issatugo
 * 
 * PROFESSOR RESPONSÁVEL:
 * - Dr. Flavio Krzyzanowski Junior
 * 
 * DATA DE FINALIZAÇÃO DO PROJETO: 10/11/2026
 * ============================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const int PIN_SENSOR_UMIDADE = A0;
const int PIN_RGB_VERDE      = 2;
const int PIN_RGB_AZUL       = 3;
const int PIN_RGB_VERMELHO   = 4;
const int PIN_RELE_LUZ       = 5;
const int PIN_RELE_BOMBA     = 6;
const int PIN_BOTAO          = 7;
const int PIN_RELE_FAN       = 8;
const int PIN_TRIG           = 9;
const int PIN_ECHO           = 10;
const int PIN_BUZZER         = 11;

const int RELE_ATIVO    = LOW; 
const int RELE_DESATIVO = HIGH;

const int VALOR_SOLO_SECO    = 1015;
const int VALOR_SOLO_MOLHADO = 360;
const int LIMITE_SOLO_SECO   = 750;

const int DIST_RESERV_CHEIO  = 10;
const int DIST_RESERV_VAZIO  = 28;

const int LIMIAR_CRITICO_ENTRA = 20;
const int LIMIAR_CRITICO_SAI   = 23;

const unsigned long TEMPO_PULSO_BOMBA = 3000;
const unsigned long TEMPO_PAUSA_BOMBA = 7000;
unsigned long tempoInicioPulsoBomba   = 0;
bool bombaEmCicloAtivo                = false;

enum ModoIluminacao { MODO_SOLAR, MODO_GROW };
ModoIluminacao modoAtual = MODO_GROW;

unsigned long tempoAnteriorRelogio = 0;
unsigned long tempoAnteriorBuzzer  = 0;
unsigned long tempoAnteriorSerial  = 0;
unsigned long tempoUltimoResetLCD  = 0;

int segundos = 0, minutos = 5, horas = 12;
bool ultimoEstadoBotao = HIGH;
unsigned long tempoUltimoDebounce = 0;
bool estadoBuzzer = false;
bool alarmeReservatorioAtivo = false;

float distAguaCm = 0.0;
int leituraSoloBruta = 0;
bool estadoLuz = false;
bool estadoBomba = false;
bool estadoFan = false;

void setRGB(bool r, bool g, bool b) {
  digitalWrite(PIN_RGB_VERMELHO, r);
  digitalWrite(PIN_RGB_VERDE, g);
  digitalWrite(PIN_RGB_AZUL, b);
}

void atualizarRelogio() {
  if (millis() - tempoAnteriorRelogio >= 1000) {
    tempoAnteriorRelogio += 1000;
    if (++segundos >= 60) {
      segundos = 0;
      if (++minutos >= 60) {
        minutos = 0;
        if (++horas >= 24) horas = 0;

        if (horas == 6) {
          modoAtual = MODO_GROW;
        }
      }
    }
  }
}

void lerBotao(bool ehDia) {
  bool leitura = digitalRead(PIN_BOTAO);
  
  if (leitura == LOW && ultimoEstadoBotao == HIGH && (millis() - tempoUltimoDebounce > 250)) {
    tempoUltimoDebounce = millis();
    modoAtual = (modoAtual == MODO_SOLAR) ? MODO_GROW : MODO_SOLAR;

    if (!ehDia && modoAtual == MODO_GROW) {
      lcd.clear();
      lcd.setCursor(2, 0);
      lcd.print("Fotossintese");
      lcd.setCursor(4, 1);
      lcd.print("Noturna");
      setRGB(LOW, LOW, HIGH);
      delay(2500);
      lcd.clear();
      modoAtual = MODO_SOLAR;
    }
  }
  ultimoEstadoBotao = leitura;
}

int lerNivelAguaPct() {
  long somaDuracoes = 0;
  int leiturasValidas = 0;

  for (int i = 0; i < 5; i++) {
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    long duracao = pulseIn(PIN_ECHO, HIGH, 25000);
    if (duracao > 0) {
      somaDuracoes += duracao;
      leiturasValidas++;
    }
    delayMicroseconds(500);
  }

  if (leiturasValidas == 0) {
    distAguaCm = -1.0;
    alarmeReservatorioAtivo = true;
    return 0;
  }

  long mediaDuracao = somaDuracoes / leiturasValidas;
  distAguaCm = (mediaDuracao * 0.0343) / 2.0;

  int pct = map((int)distAguaCm, DIST_RESERV_VAZIO, DIST_RESERV_CHEIO, 0, 100);
  pct = constrain(pct, 0, 100);

  if (pct <= LIMIAR_CRITICO_ENTRA) {
    alarmeReservatorioAtivo = true;
  } else if (pct >= LIMIAR_CRITICO_SAI) {
    alarmeReservatorioAtivo = false;
  }

  return pct;
}

bool controleClimaELuz(bool ehDia) {
  if (modoAtual == MODO_GROW && ehDia) {
    digitalWrite(PIN_RELE_LUZ, RELE_ATIVO);
    estadoLuz = true;
  } else {
    digitalWrite(PIN_RELE_LUZ, RELE_DESATIVO);
    estadoLuz = false;
  }

  bool ligarFan = false;

  if (ehDia) {
    if ((minutos >= 0 && minutos < 15) || (minutos >= 30 && minutos < 45)) {
      ligarFan = true;
    }
  } else {
    if ((horas % 2 == 0) && (minutos < 5)) {
      ligarFan = true;
    }
  }

  if (ligarFan) {
    digitalWrite(PIN_RELE_FAN, RELE_ATIVO);
    estadoFan = true;
  } else {
    digitalWrite(PIN_RELE_FAN, RELE_DESATIVO);
    estadoFan = false;
  }

  return ligarFan;
}

void controleAlarmeAgua() {
  if (alarmeReservatorioAtivo) {
    if (millis() - tempoAnteriorBuzzer >= 1000) {
      tempoAnteriorBuzzer = millis();
      estadoBuzzer = !estadoBuzzer;
      if (estadoBuzzer) {
        tone(PIN_BUZZER, 2500);
      } else {
        noTone(PIN_BUZZER);
      }
    }
  } else {
    noTone(PIN_BUZZER);
    estadoBuzzer = false;
  }
}

int controleIrrigacao(bool &seco) {
  leituraSoloBruta = analogRead(PIN_SENSOR_UMIDADE);

  int umidadePct = map(leituraSoloBruta, VALOR_SOLO_SECO, VALOR_SOLO_MOLHADO, 0, 100);
  umidadePct = constrain(umidadePct, 0, 100);

  seco = (leituraSoloBruta > LIMITE_SOLO_SECO);
  unsigned long agora = millis();

  if (seco && !alarmeReservatorioAtivo) {
    if (!bombaEmCicloAtivo) {
      if (agora - tempoInicioPulsoBomba >= TEMPO_PAUSA_BOMBA) {
        bombaEmCicloAtivo = true;
        tempoInicioPulsoBomba = agora;
      }
    } else {
      if (agora - tempoInicioPulsoBomba >= TEMPO_PULSO_BOMBA) {
        bombaEmCicloAtivo = false;
        tempoInicioPulsoBomba = agora;
      }
    }
  } else {
    if (bombaEmCicloAtivo) {
      bombaEmCicloAtivo = false;
      tempoInicioPulsoBomba = agora;
    }
  }

  if (bombaEmCicloAtivo) {
    digitalWrite(PIN_RELE_BOMBA, RELE_ATIVO);
    estadoBomba = true;
  } else {
    digitalWrite(PIN_RELE_BOMBA, RELE_DESATIVO);
    estadoBomba = false;
  }

  return umidadePct;
}

void atualizarStatusLED(bool ligarFan, bool ehDia) {
  if (alarmeReservatorioAtivo) {
    setRGB(HIGH, LOW, LOW);
  } else if (estadoBomba) {
    setRGB(HIGH, LOW, HIGH);
  } else if (ligarFan) {
    setRGB(LOW, HIGH, HIGH);
  } else if (!ehDia) {
    setRGB(LOW, LOW, HIGH);
  } else {
    setRGB(LOW, HIGH, LOW);
  }
}

void atualizarLCD(bool ehDia, int umidadePct, int nivelAguaPct, bool ligarFan) {
  unsigned long agora = millis();

  if (Wire.getWireTimeoutFlag() || (agora - tempoUltimoResetLCD >= 4000)) {
    Wire.clearWireTimeoutFlag();
    tempoUltimoResetLCD = agora;
    lcd.init();
    lcd.backlight();
  }

  char bufLinha0[17];
  snprintf(bufLinha0, sizeof(bufLinha0), "%02d:%02d %s %-5s ", 
           horas, minutos, 
           ehDia ? "DIA" : "NOI", 
           modoAtual == MODO_SOLAR ? "SOLAR" : "LED");
  bufLinha0[16] = '\0';
  lcd.setCursor(0, 0);
  lcd.print(bufLinha0);

  char bufLinha1[17];
  if (alarmeReservatorioAtivo) {
    snprintf(bufLinha1, sizeof(bufLinha1), "RESERV. CRITICO ");
  } else {
    char statusStr[4];
    if (estadoBomba) {
      strcpy(statusStr, "REG");
    } else if (ligarFan) {
      strcpy(statusStr, "FAN");
    } else {
      strcpy(statusStr, "OK ");
    }

    snprintf(bufLinha1, sizeof(bufLinha1), "U:%d%% A:%d%% %s", umidadePct, nivelAguaPct, statusStr);
    
    int len = strlen(bufLinha1);
    while (len < 16) {
      bufLinha1[len++] = ' ';
    }
    bufLinha1[16] = '\0';
  }

  lcd.setCursor(0, 1);
  lcd.print(bufLinha1);
}

void imprimirDiagnosticoSerial(bool ehDia, int umidadePct, bool seco, int nivelAguaPct) {
  if (millis() - tempoAnteriorSerial >= 1000) {
    tempoAnteriorSerial = millis();

    Serial.println(F("=================================================="));
    
    Serial.print(F("[RELOGIO] "));
    if (horas < 10) Serial.print('0');
    Serial.print(horas);
    Serial.print(':');
    if (minutos < 10) Serial.print('0');
    Serial.print(minutos);
    Serial.print(':');
    if (segundos < 10) Serial.print('0');
    Serial.print(segundos);
    Serial.print(ehDia ? F(" (DIA)") : F(" (NOITE)"));
    Serial.print(F(" | MODO: "));
    Serial.println(modoAtual == MODO_SOLAR ? F("SOLAR") : F("GROW (LED)"));

    Serial.print(F("[RESERVATORIO] Nivel: "));
    Serial.print(nivelAguaPct);
    Serial.print(F("% | Distancia Real: "));
    if (distAguaCm < 0) {
      Serial.print(F("FALHA/SEM ECO"));
    } else {
      Serial.print(distAguaCm, 1);
      Serial.print(F(" cm"));
    }
    Serial.println(alarmeReservatorioAtivo ? F(" -> [ALERTA: RESERVATORIO CRITICO <= 20%]") : F(" -> [NIVEL OK]"));

    Serial.print(F("[SOLO] Umidade: "));
    Serial.print(umidadePct);
    Serial.print(F("% | Bruto A0: "));
    Serial.print(leituraSoloBruta);
    Serial.println(seco ? F(" -> [SECO - PEDE REGA]") : F(" -> [UMIDO - OK]"));

    Serial.print(F("[RELES] Luz: "));
    Serial.print(estadoLuz ? F("LIGADA | ") : F("DESLIGADA | "));
    Serial.print(F("Cooler: "));
    Serial.print(estadoFan ? F("LIGADO | ") : F("DESLIGADO | "));
    Serial.print(F("Bomba: "));
    if (estadoBomba) {
      Serial.println(F("LIGADA (PULSO DE REGA)"));
    } else if (seco && alarmeReservatorioAtivo) {
      Serial.println(F("BLOQUEADA (AGUA <= 20%)"));
    } else if (seco && !bombaEmCicloAtivo) {
      Serial.println(F("PAUSA DO CICLO DE REGA"));
    } else {
      Serial.println(F("DESLIGADA"));
    }
  }
}

void setup() {
  Serial.begin(9600);
  
  pinMode(PIN_RGB_VERDE, OUTPUT);
  pinMode(PIN_RGB_AZUL, OUTPUT);
  pinMode(PIN_RGB_VERMELHO, OUTPUT);
  
  digitalWrite(PIN_RELE_LUZ, RELE_DESATIVO);
  pinMode(PIN_RELE_LUZ, OUTPUT);

  digitalWrite(PIN_RELE_BOMBA, RELE_DESATIVO);
  pinMode(PIN_RELE_BOMBA, OUTPUT);

  digitalWrite(PIN_RELE_FAN, RELE_DESATIVO);
  pinMode(PIN_RELE_FAN, OUTPUT);
  
  pinMode(PIN_BOTAO, INPUT_PULLUP);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  setRGB(LOW, LOW, LOW);

  lcd.init();
  Wire.setWireTimeout(3000, true);
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("SEDCITEC");
  lcd.setCursor(1, 1);
  lcd.print("A Horta de DS");
  
  Serial.println(F(">>> SEDCITEC - A Horta de DS Inicializada <<<"));
  delay(3000);
  lcd.clear();
}

void loop() {
  atualizarRelogio();
  bool ehDia = (horas >= 6 && horas < 18);

  lerBotao(ehDia);

  int nivelAguaPct = lerNivelAguaPct();
  controleAlarmeAgua();
  bool ligarFan = controleClimaELuz(ehDia);

  bool seco = false;
  int umidadePct = controleIrrigacao(seco);

  atualizarStatusLED(ligarFan, ehDia);
  atualizarLCD(ehDia, umidadePct, nivelAguaPct, ligarFan);

  imprimirDiagnosticoSerial(ehDia, umidadePct, seco, nivelAguaPct);

  delay(150);
}