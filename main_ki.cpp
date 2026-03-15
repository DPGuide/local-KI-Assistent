#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/System.hpp>
#include <SFML/Network.hpp>
#include <SFML/Audio.hpp>
#include "json.hpp"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <cmath>
#include <optional>
#include <cstdlib>
#include <chrono>
using json = nlohmann::json;
// --- KONFIGURATION (ORIGINAL OPTIK) ---
namespace Face {
    const unsigned int W = 640;          // Popup-Breite
    const unsigned int H = 480;          // Popup-Höhe
    const float FONT_SIZE = 1.01f;       // Original Matrix-Optik
}
// STATUS-FLAGS
std::atomic<bool> appRunning{true};
std::atomic<bool> thinking{false};
std::atomic<bool> listening{false};
std::atomic<bool> talking{false};
std::atomic<bool> isBooting{true};
std::atomic<bool> resetIdleTimer{true};

// UI STATE
bool isVisible = true;
bool isDragging = false;
sf::Vector2i grabOffset;

// HELPER
std::string readFile(const std::string& filename) {
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if (!in) return "";
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void logAion(const std::string& msg) {
    std::cout << "[AION LOG] " << msg << std::endl;
}

void callBrain(std::string p) {
    thinking = true;
    logAion("Analysiere: " + p);

    // 1. SYSTEM-PROMPT: Hier bringen wir AION die PC-Steuerung bei
    std::string systemPrompt = 
        "Du bist AION, ein lokaler PC-Assistent. "
        "Wenn der User dich bittet, ein Programm zu öffnen oder eine PC-Aktion auszuführen, "
        "schreibe den genauen Windows-Konsolenbefehl in eckige Klammern. "
        "Beispiel: [CMD: start notepad.exe] oder [CMD: calc]. "
        "Antworte danach kurz auf Deutsch, was du getan hast.";

    std::string fullPrompt = systemPrompt + "\n\nUser: " + p;

    // 2. HTTP-REQUEST AN DEINE LOKALE KI (Ollama) VORBEREITEN
    sf::Http http("127.0.0.1", 11434);
    sf::Http::Request request("/api/generate", sf::Http::Request::Method::Post);

    // JSON-Payload für Ollama erstellen
    json requestBody;
    requestBody["model"] = "llama3.2"; // Hier dein lokales Modell eintragen
    requestBody["prompt"] = fullPrompt;
    requestBody["stream"] = false;

    request.setBody(requestBody.dump());
    request.setField("Content-Type", "application/json");

    // 3. ANFRAGE SENDEN (Timeout auf 120 Sekunden, falls die KI länger nachdenkt)
    sf::Http::Response response = http.sendRequest(request, sf::seconds(120.f));

    if (response.getStatus() == sf::Http::Response::Status::Ok) {
        // Antwort parsen
        json responseJson = json::parse(response.getBody());
        std::string aiAnswer = responseJson["response"];

        // 4. BEFEHLE EXTRAHIEREN UND AUSFÜHREN [CMD: ...]
        size_t cmdStart = aiAnswer.find("[CMD:");
        if (cmdStart != std::string::npos) {
            size_t cmdEnd = aiAnswer.find("]", cmdStart);
            if (cmdEnd != std::string::npos) {
                // Befehl ausschneiden (Länge von "[CMD:" ist 5)
                std::string command = aiAnswer.substr(cmdStart + 5, cmdEnd - (cmdStart + 5));
                
                // Leerzeichen am Anfang entfernen, falls die KI "[CMD: notepad]" schreibt
                if (!command.empty() && command[0] == ' ') {
                    command.erase(0, 1);
                }

                logAion("FUEHRE SYSTEMBEFEHL AUS: " + command);
                
                // HIER PASSIERT DIE MAGIE: C++ führt den Befehl auf deinem PC aus!
                std::system(command.c_str());

                // Wir löschen den [CMD: ...]-Block aus dem Text, 
                // damit das Gesicht den kryptischen Befehl nicht laut vorliest.
                aiAnswer.erase(cmdStart, cmdEnd - cmdStart + 1);
            }
        }

        // 5. ANTWORT FÜR DAS GESICHT SPEICHERN
        std::ofstream out("ai_answer.txt", std::ios::trunc);
		out << aiAnswer;
		out.close();
		
		// 6. WEIBLICHE STIMME GENERIEREN (Piper)
		if (!aiAnswer.empty()) {
			talking = true;
			
			// Wir nutzen Piper mit einer deutschen Frauenstimme (z.B. ramona.onnx)
			// 'type' schickt den Inhalt der Datei an Piper
			std::string voiceCmd = "type ai_answer.txt | piper.exe --model de_DE-ramona-medium.onnx --output_file response.wav";
			std::system(voiceCmd.c_str());
		
			// Die erzeugte Datei abspielen
			std::system("powershell -c \"(New-Object Media.SoundPlayer 'response.wav').PlaySync()\"");
			
			talking = false;
        }
    } else {
        logAion("Fehler bei der Verbindung zum KI-Kern. Status: " + std::to_string(static_cast<int>(response.getStatus())));
    }

    thinking = false;
}
// --- NEU: EIGENER THREAD FÜR DIE OHREN ---
void voiceLoop() {
    sf::SoundBufferRecorder recorder;
    bool isRecording = false;

    while (appRunning) {
        // STRG LINKS als Walkie-Talkie Taste
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)) {
            if (!isRecording && !thinking && !talking) {
                isRecording = true;
                logAion("AION hoert zu... (STRG links gedrueckt halten!)");
                (void)recorder.start();
            }
        } 
        else if (isRecording) {
            isRecording = false;
            recorder.stop();
            
            const sf::SoundBuffer& buffer = recorder.getBuffer();
            
            // Sicherheitscheck: Wurde die Taste lange genug gehalten? (> 0.5 Sekunden)
            if (buffer.getDuration().asSeconds() > 0.5f) {
                logAion("Verarbeite Sprache...");
                (void)buffer.saveToFile("input.wav");

                std::system("whisper-cli.exe -m ggml-base.bin -f input.wav --language de --output-txt");

                std::ifstream ifs("input.wav.txt");
                std::string voiceText;
                if (std::getline(ifs, voiceText)) {
                    if (voiceText.length() > 2) {
                        logAion("Gehoert: " + voiceText);
                        std::thread(callBrain, voiceText).detach();
                    }
                }
                ifs.close();
            } else {
                logAion("Aufnahme abgebrochen: Taste wurde zu kurz gedrueckt!");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
// --- ZURUECK ZUM URSPRUNG: DIE KONSOLE KANN WIEDER TIPPEN ---
void consoleLoop() {
    std::string input;
    while (appRunning) {
        std::cout << "\n>> CMD: ";
        std::getline(std::cin, input);
        if (input == "exit") { appRunning = false; break; }
        
        if (!input.empty()) {
            if (thinking || talking) {
                logAion("SYSTEM-WARNUNG: Status zurueckgesetzt!");
                thinking = false;
                talking = false;
            }
            resetIdleTimer = true;
            std::thread(callBrain, input).detach();
        }
    }
}

void startupGreeting() {
    isBooting = true;
    logAion("System Booting...");
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    isBooting = false;
}

void activateVoice() {
    if (listening || thinking || talking || isBooting) return;
    listening = true;
    // Nutze internen Buffer für Echo-Schutz
    std::string spokenText = readFile("aion_internal_ears.txt"); 
    listening = false;
    if (!spokenText.empty() && spokenText.length() > 1) {
        // Buffer leeren
        std::ofstream ofs("aion_internal_ears.txt", std::ios::trunc); ofs.close();
        logAion("Voice-Input: " + spokenText);
        std::thread(callBrain, spokenText).detach();
        resetIdleTimer = true;
    }
}

int main() {
    // --- PARTNER 1: DIE KONSOLE (Der Bräutigam) ---
    HWND hwndConsole = GetConsoleWindow();
    if (hwndConsole) {
        SetWindowTextA(hwndConsole, "AION NEXUS & BRAIN");
        MoveWindow(hwndConsole, 600, 50, 640, 480, TRUE);
    }

    // --- PARTNER 2: DAS GESICHT (Die Braut) ---
    sf::RenderWindow window(sf::VideoMode({Face::W, Face::H}), "AION FACE INTERFACE", sf::Style::None); 
    window.setFramerateLimit(60);

    // Handle holen und Transparenz-Schleier anlegen
    HWND hwndFace = (HWND)window.getNativeHandle();
    SetWindowLong(hwndFace, GWL_EXSTYLE, GetWindowLong(hwndFace, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwndFace, RGB(0, 0, 0), 0, LWA_COLORKEY);

    SetConsoleOutputCP(65001);
	SetConsoleCP(65001);	
	
    sf::Font font;
    if (!font.openFromFile("font.ttf")) {
        if (!font.openFromFile("C:/Windows/Fonts/consola.ttf")) return -1;
    }

    sf::Image faceMap;
    if (!faceMap.loadFromFile("face.png")) {
        faceMap.resize(sf::Vector2u(Face::W, Face::H), sf::Color::Black);
    }
    sf::Vector2u imgSize = faceMap.getSize();

    const int COLS = static_cast<int>(Face::W / Face::FONT_SIZE);
    const int ROWS = static_cast<int>(Face::H / Face::FONT_SIZE);
    const std::string symbols = ".o00´"; 

    struct Drop { float y; float speed; };
    std::vector<std::vector<Drop>> columnDrops(COLS);
    std::vector<std::vector<char>> gridChars(COLS, std::vector<char>(ROWS));

    for(int x = 0; x < COLS; x++) {
        for(int i = 0; i < 30; i++) {
            columnDrops[x].push_back({
                static_cast<float>(rand() % Face::H), 
                static_cast<float>(rand() % 1 + 1) 
            });
        }
        for(int y = 0; y < ROWS; y++) {
            gridChars[x][y] = symbols[rand() % symbols.size()];
        }
    }

    sf::Text text(font);
    text.setCharacterSize(static_cast<unsigned int>(Face::FONT_SIZE)); 

    std::thread(startupGreeting).detach();
    std::thread(consoleLoop).detach();
	std::thread(voiceLoop).detach();

    sf::Clock idleClock, animClock, blinkClock;
    float nextBlinkTime = (float)(rand() % 3 + 2);
    bool isBlinking = false;
	
	

    // --- HAUPTSCHLEIFE ---
    // --- HAUPTSCHLEIFE ---
    while (window.isOpen() && appRunning) {
        
        // 1. NUR EINE EVENT-SCHLEIFE! (Alle Events hier abhandeln)
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) { appRunning = false; window.close(); }
            
            // Beenden mit ESC
            if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                if (keyPressed->code == sf::Keyboard::Key::Escape) { appRunning = false; window.close(); }
            }

            // Dragging-Start
            if (const auto* mouseBtn = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (mouseBtn->button == sf::Mouse::Button::Left) {
                    isDragging = true; 
                    grabOffset = sf::Mouse::getPosition(window);
                }
            }
            if (event->is<sf::Event::MouseButtonReleased>()) isDragging = false;
        }

        // 2. DIE LOGIK DER "EHE"
        if (hwndConsole) {
            if (IsIconic(hwndConsole)) {
                if (isVisible) { window.setVisible(false); isVisible = false; }
            } else {
                if (!isVisible) { window.setVisible(true); isVisible = true; }

                if (isDragging) {
                    // FALL A: Du ziehst am Gesicht -> Qanny folgt
                    sf::Vector2i mousePos = sf::Mouse::getPosition();
                    sf::Vector2i newPos = mousePos - grabOffset;
                    window.setPosition(newPos);
                    
                    // Konsole so verschieben, dass das Gesicht oben rechts bleibt
                    // 640 ist die Konsolenbreite, 320 die Gesichtsbreite
                    MoveWindow(hwndConsole, newPos.x - 0, newPos.y - 0, 640, 480, TRUE);
                } else {
                    // FALL B: Du ziehst an Qanny (CMD) -> Das Gesicht folgt (Magnet)
                    RECT rect;
                    GetWindowRect(hwndConsole, &rect);

                    // Berechnung für die Ecke oben rechts
                    int cornerX = rect.right - Face::W - 0; // 15px vom rechten Rand
                    int cornerY = rect.top + 0;             // 35px unter der Titelleiste

                    // Gesicht an diese Position zwingen
                    SetWindowPos(hwndFace, HWND_TOPMOST, cornerX, cornerY, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
        }

        // 3. GRAFIK-UPDATE (Rest deines Codes...)

    // ... (restlicher Grafik-Code)

        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) { appRunning = false; window.close(); }
            if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                if (keyPressed->code == sf::Keyboard::Key::Escape) { appRunning = false; window.close(); }
            }
        }

        if (listening || thinking || talking) resetIdleTimer = true;
        if (resetIdleTimer) {
            idleClock.restart(); resetIdleTimer = false;
            if (!isVisible) { window.setVisible(true); isVisible = true; }
        }
        
        if (isVisible && !isBooting && !thinking && !listening && !talking) {
            std::thread(activateVoice).detach();
        }

        if (isVisible) {
            window.clear(sf::Color::Black);
            float time = animClock.getElapsedTime().asSeconds();

            float blinkTimeElapsed = blinkClock.getElapsedTime().asSeconds();
            if (blinkTimeElapsed > nextBlinkTime) {
                isBlinking = true;
                if (blinkTimeElapsed > nextBlinkTime + 0.20f) {
                    isBlinking = false; blinkClock.restart(); nextBlinkTime = (float)(rand() % 4 + 2);
                }
            }

            sf::Color stateBaseColor;
            if (listening) stateBaseColor = sf::Color(255, 50, 50);       
            else if (thinking) stateBaseColor = sf::Color(50, 150, 255); 
            else stateBaseColor = sf::Color(0, 255, 100);                

            for (int x = 0; x < COLS; x++) {
                for (auto& d : columnDrops[x]) {
                    d.y += d.speed * 0.65f;
                    if (d.y > Face::H + 5) d.y = -5;
                }

                for (int y = 0; y < ROWS; y++) {
                    float pX = x * Face::FONT_SIZE;
                    float pY = y * Face::FONT_SIZE; // <--- Prüfe, ob hier das ; war!
                    
                    // Korrekte Berechnung mit dem Face-Namespace
                    unsigned int mX = static_cast<unsigned int>((pX / Face::W) * imgSize.x);
                    unsigned int mY = static_cast<unsigned int>((pY / Face::H) * imgSize.y);
                    
                    if (mX >= imgSize.x || mY >= imgSize.y) continue;

                    // SFML 3 Fix: Benutze sf::Vector2u(...) statt nur {mX, mY}
                    sf::Color pixel = faceMap.getPixel(sf::Vector2u(mX, mY));
                    int brightness = (pixel.r + pixel.g + pixel.b) / 4;

                    if (brightness > 40 && brightness < 60) {
                        text.setString(std::string(1, gridChars[x][y]));
                        text.setPosition({pX, pY});
                        text.setFillColor(sf::Color(45, 45, 45));
                        window.draw(text);
                        if (brightness < 50) continue; 
                    }
                    
                    if (brightness < 80) continue;

                    float baseInt = 0.77f;
                    sf::Color finalCol = stateBaseColor;
                    float dGlow = 0.11f;

                    for (const auto& d : columnDrops[x]) {
                        float dist = d.y - pY;
                        if (dist > 0 && dist < 80) dGlow = std::max(dGlow, (1.0f - (dist / 80.0f)));
                    }

                    float highlight = baseInt + dGlow;
                    finalCol.r = static_cast<uint8_t>(std::min(255, (int)(finalCol.r * highlight)));
                    finalCol.g = static_cast<uint8_t>(std::min(255, (int)(finalCol.g * highlight)));
                    finalCol.b = static_cast<uint8_t>(std::min(255, (int)(finalCol.b * highlight)));

                    if (dGlow > 0.9f) finalCol = sf::Color::White;

                    bool isEyeArea = (y > ROWS * 0.35 && y < ROWS * 0.45) && 
                                     ((x > COLS * 0.25 && x < COLS * 0.40) || (x > COLS * 0.60 && x < COLS * 0.75));

                    if (isBlinking && isEyeArea) continue;

                    float wave = std::sin(time * 5.0f + (x * 0.32f)) * std::cos(time * 6.0f + (y * 0.2f));

                    if (wave > 1.32f) {
                         float intensity = 10.6f + (wave * 21.11f); 
                         finalCol.r = static_cast<uint8_t>(std::min(255, (int)(finalCol.r + (255 * intensity))));
                         finalCol.g = static_cast<uint8_t>(std::min(255, (int)(finalCol.g + (255 * intensity))));
                         finalCol.b = static_cast<uint8_t>(std::min(255, (int)(finalCol.b + (255 * intensity))));
                    } else if (wave < -1.01f) {
                        finalCol.r = static_cast<uint8_t>(finalCol.r * 0.4f);
                        finalCol.g = static_cast<uint8_t>(finalCol.g * 0.5f);
                        finalCol.b = static_cast<uint8_t>(finalCol.b * 0.6f);
                    }

                    text.setString(std::string(1, gridChars[x][y]));
                    text.setPosition({pX, pY});
                    text.setFillColor(finalCol);
                    window.draw(text);
                }
            }
            window.display();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return 0;
}