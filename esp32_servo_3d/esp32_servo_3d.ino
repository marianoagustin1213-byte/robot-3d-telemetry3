#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>
#include "secrets.h" // Incluye tus credenciales de forma segura

// --- CONFIGURACIÓN DE SERVOS ---
const int NUM_SERVOS = 5;

// Instancias de los 5 servos
Servo servos[NUM_SERVOS];

// Pines para Potenciómetros (Únicamente bloque ADC1 compatible con Wi-Fi)
const int pinPots[NUM_SERVOS] = {36, 39, 34, 35, 32}; 

// Pines para Servomotores (Salidas PWM)
const int pinServos[NUM_SERVOS] = {18, 19, 21, 22, 23};

// Pines para Botones e Indicador LED
const int pinBotonGrabar = 4;      // Botón 1: Iniciar / Detener Grabación
const int pinBotonReproducir = 17; // Botón 2: 1-Play / 2-Loop / 3-Stop
const int pinLED = 2;              // LED indicador en placa

// --- CONFIGURACIÓN WI-FI LOCAL E IP ESTÁTICA ---
const char* ssid = WIFI_SSID;        // Importado desde secrets.h
const char* password = WIFI_PASSWORD;  // Importado desde secrets.h

IPAddress local_IP(192, 168, 1, 200);   // IP estática deseada para el ESP32
IPAddress gateway(192, 168, 1, 1);       // IP del módem / router
IPAddress subnet(255, 255, 255, 0);      // Máscara de subred
IPAddress primaryDNS(8, 8, 8, 8);       // Servidor DNS opcional

WebServer server(80);

// --- MEMORIA DE DATOS (250 muestras a 40ms = 10 segundos para 5 servos) ---
const int MAX_PASOS = 250;
int secuencia[MAX_PASOS][NUM_SERVOS]; // Matriz 2D: 250 pasos x 5 servos
int totalPasosGuardados = 0;
int pasoActual = 0;

// Estados del sistema
enum Estado { MANUAL, GRABANDO, REPRODUCIENDO };
Estado estadoActual = MANUAL;
bool modoLoop = false; 

// Control manual por Web (Microsegundos por servo)
int microsegundosWeb[NUM_SERVOS] = {1500, 1500, 1500, 1500, 1500};
bool usarControlWeb[NUM_SERVOS] = {false, false, false, false, false};
int ultimoValorPot[NUM_SERVOS] = {-1, -1, -1, -1, -1};

// Temporización de muestreo (40 ms = 25 lecturas/segundo)
unsigned long ultimoTiempoMuestreo = 0;
const int intervaloMuestreo = 40; 

// --- ANTIRREBOTE POR SOFTWARE (DEBOUNCE POR MILLIS) ---
const unsigned long tiempoDebounce = 50;

int estadoBotonGrabar = HIGH;
int ultimoEstadoLecturaGrabar = HIGH;
unsigned long ultimoTiempoDebounceGrabar = 0;

int estadoBotonReproducir = HIGH;
int ultimoEstadoLecturaReproducir = HIGH;
unsigned long ultimoTiempoDebounceReproducir = 0;

// Declaraciones previas de funciones
int obtenerMicrosegundosSeguros(int index);
void moverModoManual();
void grabarMuestra();
void reproducirMuestra();
void ejecutarAccionGrabar();
void ejecutarAccionReproducir();
void gestionarBotones();

// --- CÓDIGO HTML / JS CON VISUALIZADOR 3D (THREE.JS) ---
const char HTML_PAGINA[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 5-Servo Control 3D</title>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
  <style>
    body { font-family: Arial, sans-serif; background-color: #1a1a2e; color: #fff; text-align: center; margin: 0; padding: 15px; }
    .layout { display: flex; flex-wrap: wrap; gap: 15px; max-width: 1100px; margin: auto; justify-content: center; }
    .card { background: #22223b; padding: 15px; border-radius: 12px; flex: 1; min-width: 320px; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }
    h2 { color: #00d2ff; margin-top: 0; font-size: 1.3em; }
    #canvas3d { width: 100%; height: 380px; border-radius: 8px; background: #0f0f1b; }
    .status-box { padding: 10px; border-radius: 8px; font-weight: bold; margin-bottom: 15px; background: #4a4e69; font-size: 1em; }
    .btn-group { display: flex; gap: 10px; justify-content: center; margin-bottom: 15px; }
    .btn { background: #00d2ff; color: #1e1e2f; border: none; padding: 12px; font-size: 14px; font-weight: bold; border-radius: 8px; cursor: pointer; flex: 1; transition: 0.2s; }
    .btn:active { transform: scale(0.98); }
    .btn-record { background: #ff4757; color: #fff; }
    .btn-play { background: #2ed573; color: #fff; }
    .slider-container { margin: 8px 0; text-align: left; background: #1f1f33; padding: 8px 12px; border-radius: 6px; }
    label { font-size: 0.85em; color: #00d2ff; font-weight: bold; display: flex; justify-content: space-between; }
    input[type=range] { width: 100%; margin-top: 6px; accent-color: #00d2ff; }
    .val-txt { color: #aaa; font-weight: normal; }
  </style>
</head>
<body>
  <div class="layout">
    <!-- VISUALIZADOR 3D -->
    <div class="card">
      <h2>Visualizador 3D (5 Servos)</h2>
      <div id="canvas3d"></div>
    </div>

    <!-- PANEL DE CONTROL -->
    <div class="card">
      <h2>Control de Servos</h2>
      <div class="status-box" id="estadoTxt">Cargando estado...</div>

      <div class="btn-group">
        <button class="btn btn-record" onclick="enviarAccion('grabar')">🔴 Grabar / Detener</button>
        <button class="btn btn-play" onclick="enviarAccion('reproducir')">▶️ Play / Loop / Stop</button>
      </div>

      <div id="slidersGroup"></div>
    </div>
  </div>

  <script>
    // Generar sliders
    const container = document.getElementById('slidersGroup');
    for (let i = 0; i < 5; i++) {
      container.innerHTML += `
        <div class="slider-container">
          <label>Servo ${i + 1} <span class="val-txt"><span id="valUs${i}">1500</span> us</span></label>
          <input type="range" id="sliderServo${i}" min="850" max="2150" value="1500" oninput="moverServoWeb(${i}, this.value)">
        </div>
      `;
    }

    // --- ESCENA THREE.JS (MODELO 3D DE 5 ARTICULACIONES) ---
    const container3d = document.getElementById('canvas3d');
    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(45, container3d.clientWidth / container3d.clientHeight, 0.1, 1000);
    const renderer = new THREE.WebGLRenderer({ antialias: true });
    
    renderer.setSize(container3d.clientWidth, container3d.clientHeight);
    container3d.appendChild(renderer.domElement);

    // Luces
    const ambientLight = new THREE.AmbientLight(0xffffff, 0.6);
    scene.add(ambientLight);
    const dirLight = new THREE.DirectionalLight(0x00d2ff, 0.8);
    dirLight.position.set(10, 20, 15);
    scene.add(dirLight);

    // Grilla
    const grid = new THREE.GridHelper(20, 20, 0x00d2ff, 0x333355);
    scene.add(grid);

    // Creación de eslabones articulados (Brazo de 5 Servos)
    const matBase = new THREE.MeshPhongMaterial({ color: 0x444466 });
    const matJoint = new THREE.MeshPhongMaterial({ color: 0x00d2ff });
    const matArm = new THREE.MeshPhongMaterial({ color: 0xff4757 });

    // Servo 1: Base Giratoria (Eje Y)
    const baseGroup = new THREE.Group();
    const baseMesh = new THREE.Mesh(new THREE.CylinderGeometry(2, 2.5, 0.8, 16), matBase);
    baseGroup.add(baseMesh);
    scene.add(baseGroup);

    // Servo 2: Hombro (Eje Z)
    const joint2 = new THREE.Group();
    joint2.position.y = 0.8;
    baseGroup.add(joint2);
    const arm1 = new THREE.Mesh(new THREE.BoxGeometry(0.6, 3, 0.6), matArm);
    arm1.position.y = 1.5;
    joint2.add(arm1);

    // Servo 3: Codo (Eje Z)
    const joint3 = new THREE.Group();
    joint3.position.y = 3;
    joint2.add(joint3);
    const arm2 = new THREE.Mesh(new THREE.BoxGeometry(0.5, 2.5, 0.5), matJoint);
    arm2.position.y = 1.25;
    joint3.add(arm2);

    // Servo 4: Muñeca / Inclinación (Eje Z)
    const joint4 = new THREE.Group();
    joint4.position.y = 2.5;
    joint3.add(joint4);
    const arm3 = new THREE.Mesh(new THREE.BoxGeometry(0.4, 1.5, 0.4), matArm);
    arm3.position.y = 0.75;
    joint4.add(arm3);

    // Servo 5: Pinza / Rotación Final (Eje X)
    const joint5 = new THREE.Group();
    joint5.position.y = 1.5;
    joint4.add(joint5);
    const gripper = new THREE.Mesh(new THREE.BoxGeometry(1.2, 0.3, 0.3), matJoint);
    gripper.position.y = 0.2;
    joint5.add(gripper);

    camera.position.set(8, 8, 12);
    camera.lookAt(0, 3, 0);

    // Animación continua
    function animate() {
      requestAnimationFrame(animate);
      renderer.render(scene, camera);
    }
    animate();

    // Convertir µs (850 - 2150) a radianes (-PI/2 a PI/2)
    function usToRad(us) {
      return ((us - 850) / (2150 - 850)) * Math.PI - (Math.PI / 2);
    }

    // Actualizar rotaciones del modelo 3D
    function actualizarModelo3D(usArr) {
      baseGroup.rotation.y = usToRad(usArr[0]);
      joint2.rotation.z = usToRad(usArr[1]);
      joint3.rotation.z = usToRad(usArr[2]);
      joint4.rotation.z = usToRad(usArr[3]);
      joint5.rotation.x = usToRad(usArr[4]);
    }

    function actualizarEstado() {
      fetch('/status')
        .then(res => res.json())
        .then(data => {
          let txt = "Estado: " + data.estado;
          if(data.modoLoop) txt += " (LOOP)";
          txt += "<br><small>Pasos: " + data.pasos + " / " + data.maxPasos + "</small>";
          document.getElementById('estadoTxt').innerHTML = txt;
          
          actualizarModelo3D(data.us);

          data.us.forEach((val, i) => {
            document.getElementById('valUs' + i).innerText = val;
            let slider = document.getElementById('sliderServo' + i);
            if (!slider.matches(':focus')) {
              slider.value = val;
            }
          });
        });
    }

    function enviarAccion(act) {
      fetch('/cmd?action=' + act);
    }

    function moverServoWeb(id, val) {
      document.getElementById('valUs' + id).innerText = val;
      fetch('/cmd?action=servo&id=' + id + '&val=' + val);
    }

    setInterval(actualizarEstado, 200);

    window.addEventListener('resize', () => {
      camera.aspect = container3d.clientWidth / container3d.clientHeight;
      camera.updateProjectionMatrix();
      renderer.setSize(container3d.clientWidth, container3d.clientHeight);
    });
  </script>
</body>
</html>
)rawliteral";

// --- MANEJADORES DEL SERVIDOR WEB ---
void handleRoot() {
  server.send(200, "text/html", HTML_PAGINA);
}

void handleStatus() {
  String estadoStr = "MANUAL";
  if (estadoActual == GRABANDO) estadoStr = "GRABANDO 🔴";
  if (estadoActual == REPRODUCIENDO) estadoStr = "REPRODUCIENDO ▶️";

  String json = "{";
  json += "\"estado\":\"" + estadoStr + "\",";
  json += "\"modoLoop\":" + String(modoLoop ? "true" : "false") + ",";
  json += "\"pasos\":" + String(totalPasosGuardados) + ",";
  json += "\"maxPasos\":" + String(MAX_PASOS) + ",";
  json += "\"us\":[";
  
  for (int i = 0; i < NUM_SERVOS; i++) {
    int usActual = (estadoActual == REPRODUCIENDO && totalPasosGuardados > 0) 
                   ? secuencia[pasoActual][i] 
                   : obtenerMicrosegundosSeguros(i);
    json += String(usActual);
    if (i < NUM_SERVOS - 1) json += ",";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void handleCmd() {
  if (server.hasArg("action")) {
    String act = server.arg("action");

    if (act == "grabar") {
      ejecutarAccionGrabar();
    } 
    else if (act == "reproducir") {
      ejecutarAccionReproducir();
    } 
    else if (act == "servo" && server.hasArg("id") && server.hasArg("val")) {
      int id = server.arg("id").toInt();
      int val = server.arg("val").toInt();
      if (id >= 0 && id < NUM_SERVOS) {
        microsegundosWeb[id] = val;
        usarControlWeb[id] = true;
      }
    }
  }
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(10); // Lectura analógica de 0 a 1023

  pinMode(pinBotonGrabar, INPUT_PULLUP);
  pinMode(pinBotonReproducir, INPUT_PULLUP);
  pinMode(pinLED, OUTPUT);

  // 1. Configurar IP Estática
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    Serial.println("Error al configurar IP estática");
  }

  // 2. Conectar a red Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("\nConectando a Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n--- Conexión exitosa ---");
  Serial.print("Dirección IP Fija: http://");
  Serial.println(WiFi.localIP());

  // 3. Configurar rutas del servidor Web
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/cmd", handleCmd);
  server.begin();

  // 4. Configurar temporizadores PWM y 5 Servos
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i].setPeriodHertz(50);
    servos[i].attach(pinServos[i], 500, 2500);
  }

  Serial.println("--- Sistema Multi-Servo (5 Servos) + Visualizador 3D listo ---");
}

void loop() {
  server.handleClient(); // Atiende peticiones web
  gestionarBotones();

  switch (estadoActual) {
    case MANUAL:
      digitalWrite(pinLED, LOW);
      moverModoManual();
      break;

    case GRABANDO:
      digitalWrite(pinLED, HIGH);
      moverModoManual();
      grabarMuestra();
      break;

    case REPRODUCIENDO:
      if (modoLoop) {
        digitalWrite(pinLED, (millis() / 80) % 2);  // Parpadeo rápido = MODO LOOP
      } else {
        digitalWrite(pinLED, (millis() / 250) % 2); // Parpadeo lento = UNA VEZ
      }
      reproducirMuestra();
      break;
  }

  // --- TELEMETRÍA SERIE DE 5 SERVOS CADA 20ms ---
  static unsigned long ultimoEnvio3D = 0;
  if (millis() - ultimoEnvio3D >= 20) { 
    ultimoEnvio3D = millis();

    for (int i = 0; i < NUM_SERVOS; i++) {
      int usActual = (estadoActual == REPRODUCIENDO && totalPasosGuardados > 0) 
                     ? secuencia[pasoActual][i] 
                     : obtenerMicrosegundosSeguros(i);
      
      float anguloGrados = map(usActual, 850, 2150, 0, 180);
      Serial.print(anguloGrados);
      if (i < NUM_SERVOS - 1) Serial.print(",");
    }
    Serial.println();
  }
}

// Lee potenciómetro individual o slider web
int obtenerMicrosegundosSeguros(int index) {
  int valorPot = analogRead(pinPots[index]);

  if (abs(valorPot - ultimoValorPot[index]) > 15) {
    usarControlWeb[index] = false;
    ultimoValorPot[index] = valorPot;
  }

  if (usarControlWeb[index]) {
    return microsegundosWeb[index];
  }

  if (valorPot < 20) valorPot = 20;
  if (valorPot > 1000) valorPot = 1000;

  return map(valorPot, 20, 1000, 850, 2150);
}

void moverModoManual() {
  for (int i = 0; i < NUM_SERVOS; i++) {
    int microsegundos = obtenerMicrosegundosSeguros(i);
    servos[i].writeMicroseconds(microsegundos);
  }
}

void grabarMuestra() {
  if (millis() - ultimoTiempoMuestreo >= intervaloMuestreo) {
    ultimoTiempoMuestreo = millis();

    if (pasoActual < MAX_PASOS) {
      for (int i = 0; i < NUM_SERVOS; i++) {
        secuencia[pasoActual][i] = obtenerMicrosegundosSeguros(i);
      }
      pasoActual++;
    } else {
      totalPasosGuardados = pasoActual;
      estadoActual = MANUAL;
      Serial.println("Memoria llena. Grabación finalizada.");
    }
  }
}

void reproducirMuestra() {
  if (totalPasosGuardados == 0) {
    Serial.println("No hay datos grabados en memoria.");
    estadoActual = MANUAL;
    modoLoop = false;
    return;
  }

  if (millis() - ultimoTiempoMuestreo >= intervaloMuestreo) {
    ultimoTiempoMuestreo = millis();

    if (pasoActual < totalPasosGuardados) {
      for (int i = 0; i < NUM_SERVOS; i++) {
        servos[i].writeMicroseconds(secuencia[pasoActual][i]);
      }
      pasoActual++;
    } else {
      if (modoLoop) {
        pasoActual = 0;
      } else {
        Serial.println("Reproducción completada.");
        estadoActual = MANUAL;
      }
    }
  }
}

void ejecutarAccionGrabar() {
  if (estadoActual == GRABANDO) {
    totalPasosGuardados = pasoActual;
    estadoActual = MANUAL;
    modoLoop = false;
    Serial.print("Grabación detenida. Pasos guardados: ");
    Serial.println(totalPasosGuardados);
  } else {
    estadoActual = GRABANDO;
    pasoActual = 0;
    totalPasosGuardados = 0;
    modoLoop = false;
    Serial.println("Grabando 5 servos...");
  }
}

void ejecutarAccionReproducir() {
  if (estadoActual == MANUAL && totalPasosGuardados > 0) {
    estadoActual = REPRODUCIENDO;
    modoLoop = false;
    pasoActual = 0;
    Serial.println("Reproduciendo 1 vez...");
  } 
  else if (estadoActual == REPRODUCIENDO && !modoLoop) {
    modoLoop = true;
    Serial.println("Modo LOOP activado.");
  } 
  else if (estadoActual == REPRODUCIENDO && modoLoop) {
    estadoActual = MANUAL;
    modoLoop = false;
    Serial.println("Reproducción detenida.");
  }
}

void gestionarBotones() {
  int lecturaGrabar = digitalRead(pinBotonGrabar);
  int lecturaReproducir = digitalRead(pinBotonReproducir);

  if (lecturaGrabar != ultimoEstadoLecturaGrabar) {
    ultimoTiempoDebounceGrabar = millis();
  }

  if ((millis() - ultimoTiempoDebounceGrabar) > tiempoDebounce) {
    if (lecturaGrabar != estadoBotonGrabar) {
      estadoBotonGrabar = lecturaGrabar;
      if (estadoBotonGrabar == LOW) {
        ejecutarAccionGrabar();
      }
    }
  }
  ultimoEstadoLecturaGrabar = lecturaGrabar;

  if (lecturaReproducir != ultimoEstadoLecturaReproducir) {
    ultimoTiempoDebounceReproducir = millis();
  }

  if ((millis() - ultimoTiempoDebounceReproducir) > tiempoDebounce) {
    if (lecturaReproducir != estadoBotonReproducir) {
      estadoBotonReproducir = lecturaReproducir;
      if (estadoBotonReproducir == LOW) {
        ejecutarAccionReproducir();
      }
    }
  }
  ultimoEstadoLecturaReproducir = lecturaReproducir;
}
