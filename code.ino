#include <Wire.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>

// ---------- Broches ----------
const int PIN_MOTEUR_IN3   = 14;  // avant
const int PIN_MOTEUR_IN4   = 13;  // arrière
const int PIN_SERVO        = 26;
const int PIN_KLAXON       = 4;
const int PIN_INTERRUPTEUR = 32;  // clignotant
const int PIN_CLIGNOTANT   = 18;
const int PIN_PHARE        = 23;
const int PIN_JOY_SW       = 5;

// ---------- ADS7830 ----------
const byte ADS_ADDR = 0x48;
const byte CH_VRY = 0;  // accélération
const byte CH_VRX = 1;  // direction
const byte CH_LUM = 2;  // luminosité

// ---------- Réglages ----------
const bool INVERSER_ACCEL = false;      // mets true si le sens est inversé
const bool INVERSER_DIRECTION = false;  // mets true si gauche/droite sont inversés
const float ZONE_MORTE = 0.10;          // ignore les petits mouvements du joystick
const int SEUIL_ALLUMER = 35;
const int SEUIL_ETEINDRE = 50;

// ---------- Objets ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);  // adapte si ton écran est 20x4
Servo servoDirection;

// ---------- État ----------
float vitesse = 0;  // de -50 à 100
bool phareAllume = false;
bool klaxonActif = false;

unsigned long tVitesse = 0, tServo = 0, tLcd = 0;

// ---------- Caractères personnalisés ----------
byte flecheAvant[8]  = {B00100, B01110, B10101, B00100, B00100, B00100, B00100, B00000};
byte flecheArriere[8] = {B00100, B00100, B00100, B00100, B10101, B01110, B00100, B00000};
byte ampoule[8]      = {B01110, B10001, B10001, B10001, B01010, B01110, B01110, B00100};

int lireCanal(byte canal) {
  byte cmd = 0x84 | ((((canal << 2) | (canal >> 1)) & 0x07) << 4);
  Wire.beginTransmission(ADS_ADDR);
  Wire.write(cmd);
  Wire.endTransmission();
  Wire.requestFrom(ADS_ADDR, (byte)1);
  return Wire.available() ? Wire.read() : 128;
}

void appliquerMoteur() {
  int pwm = 0;
  if (abs(vitesse) > 0.5) 
    pwm = (int)(80 + (abs(vitesse) / 100.0) * (255 - 80));
  
  if (vitesse > 0) {
    analogWrite(PIN_MOTEUR_IN3, pwm);
    analogWrite(PIN_MOTEUR_IN4, 0);
  } else if (vitesse < 0) {
    analogWrite(PIN_MOTEUR_IN3, 0);
    analogWrite(PIN_MOTEUR_IN4, pwm);
  } else {
    analogWrite(PIN_MOTEUR_IN3, 0);
    analogWrite(PIN_MOTEUR_IN4, 0);
  }
}

void majVitesse(float dt) {
  float pousse = (lireCanal(CH_VRY) - 128) / 127.0;  // -1 à +1
  if (INVERSER_ACCEL) pousse = -pousse;
  if (fabs(pousse) < ZONE_MORTE) pousse = 0;

  // Accélération proportionnelle : max 5 u/s en avant, 3 u/s en arrière
  if (pousse > 0) vitesse += pousse * 5.0 * dt;
  else            vitesse += pousse * 3.0 * dt;

  // Décélération naturelle de 1 u/s vers 0
  if (vitesse > 0)      vitesse = max(0.0f, vitesse - 1.0f * dt);
  else if (vitesse < 0) vitesse = min(0.0f, vitesse + 1.0f * dt);

  vitesse = constrain(vitesse, -50.0f, 100.0f);
  appliquerMoteur();
}

void majServo() {
  int x = lireCanal(CH_VRX);
  int angle = map(x, 0, 255, 0, 180);
  if (INVERSER_DIRECTION) angle = 180 - angle;
  servoDirection.write(angle);
}

void majLcd(bool clignotantActif) {
  int vit = (int)round(abs(vitesse));
  lcd.setCursor(0, 0);
  lcd.print("Vitesse: ");
  if (vit < 100) lcd.print(" ");
  if (vit < 10)  lcd.print(" ");
  lcd.print(vit);
  lcd.print("   ");

  // Direction : flèche en haut à droite
  lcd.setCursor(15, 0);
  if (vitesse > 0.5)       lcd.write(byte(0));
  else if (vitesse < -0.5) lcd.write(byte(1));
  else                     lcd.print(" ");

  // Clignotant : ampoule en bas à droite
  lcd.setCursor(15, 1);
  if (clignotantActif) lcd.write(byte(2));
  else                 lcd.print(" ");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(21, 22);

  lcd.init();
  delay(100);
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, flecheAvant);
  lcd.createChar(1, flecheArriere);
  lcd.createChar(2, ampoule);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Demarrage...");

  pinMode(PIN_INTERRUPTEUR, INPUT_PULLUP);
  pinMode(PIN_JOY_SW, INPUT_PULLUP);
  pinMode(PIN_CLIGNOTANT, OUTPUT);
  pinMode(PIN_PHARE, OUTPUT);
  pinMode(PIN_MOTEUR_IN3, OUTPUT);
  pinMode(PIN_MOTEUR_IN4, OUTPUT);

  ESP32PWM::allocateTimer(0);
  servoDirection.attach(PIN_SERVO, 500, 2400);
  servoDirection.write(90);

  tVitesse = tServo = tLcd = millis();
}

void loop() {
  unsigned long now = millis();

  // Vitesse : toutes les 50 ms (20 fois par seconde)
  if (now - tVitesse >= 50) {
    float dt = (now - tVitesse) / 1000.0;
    tVitesse = now;
    majVitesse(dt);
  }

  // Servo : toutes les 250 ms (4 fois par seconde)
  if (now - tServo >= 250) {
    tServo = now;
    majServo();
  }

  // Clignotant : 250 ms éteint, 500 ms allumé
  bool clignotantActif = (digitalRead(PIN_INTERRUPTEUR) == LOW);
  if (clignotantActif) digitalWrite(PIN_CLIGNOTANT, (now % 750) >= 250);
  else                 digitalWrite(PIN_CLIGNOTANT, LOW);

  // Phare automatique avec deux seuils
  int lum = lireCanal(CH_LUM);
  if (lum < SEUIL_ALLUMER) phareAllume = true;
  else if (lum > SEUIL_ETEINDRE) phareAllume = false;
  digitalWrite(PIN_PHARE, phareAllume);

  // Klaxon : actif tant que le joystick est enfoncé
  bool appuye = (digitalRead(PIN_JOY_SW) == LOW);
  if (appuye && !klaxonActif) {
    tone(PIN_KLAXON, 1000);
    klaxonActif = true;
  } else if (!appuye && klaxonActif) {
    noTone(PIN_KLAXON);
    klaxonActif = false;
  }

  // LCD : toutes les 100 ms
  if (now - tLcd >= 100) {
    tLcd = now;
    majLcd(clignotantActif);
  }
}