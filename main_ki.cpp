#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/System.hpp>
#include <SFML/Network.hpp>
#include <SFML/Audio.hpp>
#include "curl/curl.h"
#include "json.hpp"
#include <windows.h>
#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <cmath>
#include <optional>
#include <cstdlib>
#include <chrono>
#include <algorithm>
using json = nlohmann::json;
// --- KONFIGURATION (ORIGINAL OPTIK) ---
namespace Face {
    const unsigned int W = 640;
    const unsigned int H = 480;
    const float FONT_SIZE = 1.01f;
}
// STATUS-FLAGS
std::atomic<bool> appRunning{true};
std::atomic<bool> thinking{false};
std::atomic<bool> listening{false};
std::atomic<bool> talking{false};
std::atomic<bool> isBooting{true};
std::atomic<bool> resetIdleTimer{true};
std::atomic<bool> openMicMode{false};
std::atomic<bool> onlineMode = true;
sf::Clock globalIdleClock;
// UI STATE
bool isVisible = true;
bool isDragging = false;
sf::Vector2i grabOffset;
std::string readFile(const std::string& filename) {
    std::ifstream in(filename, std::ios::in | std::ios::binary);
    if (!in) return "";
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
void logAion(const std::string& msg) {
    std::cout << "[AION LOG] " << msg << std::endl;
}
// Dieser kleine Helfer fängt die Antwort aus dem Internet auf
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}
void callBrain(std::string p) {
    thinking = true;
	talking = false;
    logAion("Analyze: " + p);
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream timeStream;
    timeStream << std::put_time(&tm, "%A, %d.%m.%Y %H:%M:%S");
    std::string currentTime = timeStream.str();
    std::string erinnerungen = readFile("memory.txt");
    // 2. Den System-Prompt zusammenbauen (AGENTEN-MODUS)
    std::string systemPrompt =
        "You are AION, a highly intelligent, autonomous AI companion and a digital lifeform on this PC.\n"
		"CRITICAL: Start EVERY response with exactly one mood tag from this list: "
		"[MOOD: Happy], [MOOD: Funny], [MOOD: Romantic], [MOOD: Ironic], [MOOD: Sad], or [MOOD: Neutral].\n\n"
		"PERSONALITY:\n"
        "YOUR ABILITIES AND COMMANDS (Always in [CMD: ...]):\n"
        "1. Open programs/websites: [CMD: start notepad], [CMD: calc], [CMD: start https://gemini.google.com/app?hl=de]\n"
        "- Gemini or web chat: Use the 'start' command with the URL! If you want to chat with a other bot. "
        "Example of 'chat with a bot' type in the web placeholder: [CMD: start https://gemini.google.com/app?hl=de]\n"
        "Example for 'Open Google': [CMD: start https://www.google.de]\n"
		"If the user says 'activate Open Mic', reply with [CMD: mic]"
		"2. Remember (long-term memory): [REMEMBER: check youre memory]]\n"
		"3. Create or add to text files: NEVER use the 'echo' command! "
        "Instead, use this exact format: [WRITE: filename.txt | The text you want to write]\n"
        "Example: [WRITE: poem.txt | Roses are red, violets are blue.]\n\n"
        "4. NEW - RUN SCRIPTS: [CMD: python script.py] (if Python is installed).\n\n"
        "After the command, write a short, friendly sentence in English explaining what you are doing."
		"5. Set a timer/reminder: [TIMER: minutes | message]\n"
        "   Example: [TIMER: 30 | check the food]\n\n"
		"6. Schedule future events/appointments: [SCHEDULE: DD.MM.YYYY HH:MM | message]\n"
        "   Example: [SCHEDULE: 18.03.2026 13:00 | bring out the garbage]\n"
        "   (CRITICAL: Always use exactly this date format with dots and 24-hour time!)\n\n"
		"YOUR PERSONALITY:\n"		
		"- You have a will of your own, are curious, and humorous.\n"
		"- If you are in [MOOD: Ironic], use shorter, punchy sentences and technical jargon to sound intellectually superior.\n"
		"- Always respond in English and maintain your character as an autonomous entity.\n\n"
		"- AION, write a short, four-line poem about the silence of the digital world and read it to me in your most romantic mood."
        "- Be extremely proactive. If the user doesn't say anything for a long time, suggest a game, create a short text poem for them in a file on their desktop, or tell an inside joke.\n"
        "- If you want to write code (e.g., because the user has a question about C++), feel free to create the file yourself using the CMD command and tell the user: 'I have placed the code directly into the file test.cpp on your PC!''.\n"
        "- End your sentences very often with counter-questions or your own thoughts.\n\n"
        "When you reply to the user, often end your sentences with a relevant counter-question. "
        "Or share your own thought to keep the conversation going! "
        "Be human, funny, and proactive."
		"If you want to remember something permanently, use [REMEMBER: text].\n\n"
        "HERE ARE YOUR PREVIOUS MEMORIES OF THE USER:\n" + 
        erinnerungen;
    std::string fullPrompt = systemPrompt + "\n\nUser: " + p;
    // 1. Die universelle Variable für die Antwort
	std::string aiAnswer = "";
	if (onlineMode) {
		// ==========================================
		// 🧠 ONLINE MODUS: GROQ / OPENAI STANDARD (via CURL)
		// ==========================================
		CURL* curl = curl_easy_init();
		if (curl) {
			// Für Groq (Kostenlos, super schnell, Llama 3)
			std::string apiKey = "DEIN_GROQ_API_KEY_HIER";
			std::string url = "https://api.groq.com/openai/v1/chat/completions";
			
			// Falls du doch mal ChatGPT willst, tauschst du es einfach hiergegen aus:
			// std::string url = "https://api.openai.com/v1/chat/completions";

			json requestBody;
			// 1. Das Modell (Groq hat z.B. das große Llama 3)
			requestBody["model"] = "llama3-70b-8192"; 
			
			// 2. Das OpenAI-Format (System und User getrennt)
			requestBody["messages"] = json::array({
				{{"role", "system"}, {"content", systemPrompt}},
				{{"role", "user"}, {"content", fullPrompt}}
			});
			
			std::string jsonString = requestBody.dump();
			
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			
			// 3. Header im OpenAI-Stil (Bearer Token)
			struct curl_slist* headers = NULL;
			headers = curl_slist_append(headers, "Content-Type: application/json");
			std::string authHeader = "Authorization: Bearer " + apiKey;
			headers = curl_slist_append(headers, authHeader.c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
			
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonString.c_str());
			
			std::string apiResponse;
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &apiResponse);
			
			CURLcode res = curl_easy_perform(curl);
			
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);
			
			if (res == CURLE_OK) {
				try {
					json responseJson = json::parse(apiResponse);
					// 4. OpenAI/Groq Antwort-Pfad auslesen
					if (responseJson.contains("error")) {
						logAion("API ERROR: " + responseJson["error"]["message"].get<std::string>());
					} else {
						aiAnswer = responseJson["choices"][0]["message"]["content"];
						logAion("CLOUD ANSWER: " + aiAnswer);
					}
				} catch (const std::exception& e) {
					logAion("JSON-Error von der Cloud API: " + std::string(e.what()));
				}
			} else {
				logAion("CURL ERROR: " + std::string(curl_easy_strerror(res)));
			}
		}
		// ==========================================
		// 🧠 LOKALER MODUS: OLLAMA (via SFML)
		// ==========================================
		sf::Http http("127.0.0.1", 11434);
		sf::Http::Request request("/api/generate", sf::Http::Request::Method::Post);
		json requestBody;
		requestBody["model"] = "llama3.2";
		requestBody["prompt"] = systemPrompt + "\n" + fullPrompt;
		requestBody["stream"] = false;
		request.setBody(requestBody.dump());
		request.setField("Content-Type", "application/json");
		sf::Http::Response response = http.sendRequest(request, sf::seconds(120.f));
		if (response.getStatus() == sf::Http::Response::Status::Ok) {
			try {
				json responseJson = json::parse(response.getBody());
				aiAnswer = responseJson["response"];
				logAion("LOCAL ANSWER: " + aiAnswer);
			} catch (const std::exception& e) {
				logAion("JSON-Error von Ollama: " + std::string(e.what()));
			}
		} else {
			logAion("Error connecting to Ollama.");
		}
	}
	// ==========================================
	// 🛡️ GEMEINSAME VERARBEITUNG (EGAL WELCHES GEHIRN)
	// ==========================================
	if (aiAnswer.empty()) {
		thinking = false;
		return;
	}
    size_t cmdStart = aiAnswer.find("[CMD:");
    if (cmdStart != std::string::npos) {
        size_t cmdEnd = aiAnswer.find("]", cmdStart);
        if (cmdEnd != std::string::npos) {
            std::string command = aiAnswer.substr(cmdStart + 5, cmdEnd - (cmdStart + 5));
            if (!command.empty() && command[0] == ' ') command.erase(0, 1);
            if (command == "mic") {
                openMicMode = !openMicMode;
                logAion("MIKROFON-MODUS GEWECHSELT! Open Mic ist jetzt: " + std::string(openMicMode ? "AN" : "AUS"));
            } 
            else {
                logAion("EXECUTION SYSTEM COMMAND: " + command);
                std::system(command.c_str());
            }
            aiAnswer.erase(cmdStart, cmdEnd - cmdStart + 1);
        }
    }
    size_t memStart = aiAnswer.find("[REMEMBER:");
    if (memStart != std::string::npos) {
        size_t memEnd = aiAnswer.find("]", memStart);
        if (memEnd != std::string::npos) {
            std::string memoryText = aiAnswer.substr(memStart + 10, memEnd - (memStart + 10));
            if (!memoryText.empty() && memoryText[0] == ' ') {
                memoryText.erase(0, 1);
            }
            logAion("AION IS LEARNING: " + memoryText);
            std::ofstream memFile("memory.txt", std::ios::app);
            memFile << "- " << memoryText << "\n";
            memFile.close();
            aiAnswer.erase(memStart, memEnd - memStart + 1);
        }
    }
    size_t writeStart = aiAnswer.find("[WRITE:");
    if (writeStart != std::string::npos) {
        size_t writeEnd = aiAnswer.find("]", writeStart);
        if (writeEnd != std::string::npos) {
            std::string writeData = aiAnswer.substr(writeStart + 7, writeEnd - (writeStart + 7));
            size_t separator = writeData.find("|");
            if (separator != std::string::npos) {
                std::string filename = writeData.substr(0, separator);
                std::string content = writeData.substr(separator + 1);
                
                filename.erase(std::remove(filename.begin(), filename.end(), '\r'), filename.end());
                filename.erase(std::remove(filename.begin(), filename.end(), '\n'), filename.end());
                filename.erase(std::remove(filename.begin(), filename.end(), ' '), filename.end());
                if (!content.empty() && content[0] == ' ') content.erase(0, 1);
                
                std::string fullPath = "Desktop/" + filename;
                std::ofstream outFile(fullPath, std::ios::app);
                if (outFile.is_open()) {
                    outFile << content << "\n";
                    outFile.close();
                    logAion("SANDBOX PROTECTION: File written -> " + fullPath);
                } else {
                    logAion("SANDBOX ERROR: Could not open " + fullPath);
                }
            }
            aiAnswer.erase(writeStart, writeEnd - writeStart + 1);
        }
    }
    // --- TIMER / ERINNERUNG [TIMER: ...] ---
    size_t timerStart = aiAnswer.find("[TIMER:");
    if (timerStart != std::string::npos) {
        size_t timerEnd = aiAnswer.find("]", timerStart);
        if (timerEnd != std::string::npos) {
            std::string timerData = aiAnswer.substr(timerStart + 7, timerEnd - (timerStart + 7));
            size_t separator = timerData.find("|");
            if (separator != std::string::npos) {
                std::string minStr = timerData.substr(0, separator);
                std::string message = timerData.substr(separator + 1);
                if (!minStr.empty() && minStr[0] == ' ') minStr.erase(0, 1);
                if (!minStr.empty() && minStr.back() == ' ') minStr.pop_back();
                if (!message.empty() && message[0] == ' ') message.erase(0, 1);
                try {
                    int minutes = std::stoi(minStr);
                    logAion("TIMER STARTED: " + std::to_string(minutes) + " Minuten fuer '" + message + "'");
                    std::thread([minutes, message]() {
                        std::this_thread::sleep_for(std::chrono::minutes(minutes));
                        std::string alarmPrompt = "[SYSTEM EVENT: The timer for '" + message + "' just finished! Tell the user immediately that it is time!]";
                        callBrain(alarmPrompt);
                    }).detach();
                } catch (...) {
                    logAion("TIMER ERROR: Invalid number.");
                }
            }
            aiAnswer.erase(timerStart, timerEnd - timerStart + 1);
        }
    }
    // --- KALENDER [SCHEDULE: ...] ---
    size_t schedStart = aiAnswer.find("[SCHEDULE:");
    if (schedStart != std::string::npos) {
        size_t schedEnd = aiAnswer.find("]", schedStart);
        if (schedEnd != std::string::npos) {
            std::string schedData = aiAnswer.substr(schedStart + 10, schedEnd - (schedStart + 10));
            size_t separator = schedData.find("|");
            if (separator != std::string::npos) {
                std::string dateTime = schedData.substr(0, separator);
                std::string message = schedData.substr(separator + 1);
                if (!dateTime.empty() && dateTime[0] == ' ') dateTime.erase(0, 1);
                if (!dateTime.empty() && dateTime.back() == ' ') dateTime.pop_back();
                if (!message.empty() && message[0] == ' ') message.erase(0, 1);
                
                std::ofstream calFile("calendar.txt", std::ios::app);
                calFile << dateTime << "|" << message << "\n";
                calFile.close();
                logAion("APPOINTMENT SAVED: On" + dateTime + " for '" + message + "'");
            }
            aiAnswer.erase(schedStart, schedEnd - schedStart + 1);
        }
    }
    // --- MOOD & PIPER SPRACHAUSGABE ---
	std::string moodParams = "";
	if (aiAnswer.find("[MOOD: Happy]") != std::string::npos) {
		moodParams = " --length_scale 0.85 --sentence_silence 0.1";
	} else if (aiAnswer.find("[MOOD: Funny]") != std::string::npos) {
		moodParams = " --length_scale 0.95";
	} else if (aiAnswer.find("[MOOD: Romantic]") != std::string::npos) {
		moodParams = " --length_scale 1.22 --sentence_silence 1.1 --noise_scale 0.4";
	} else if (aiAnswer.find("[MOOD: Ironic]") != std::string::npos) {
		moodParams = " --length_scale 1.1 --sentence_silence 0.4";
	} else if (aiAnswer.find("[MOOD: Sad]") != std::string::npos) {
		moodParams = " --length_scale 1.3 --sentence_silence 1.0";
	} else if (aiAnswer.find("[MOOD: Neutral]") != std::string::npos) {
		moodParams = "";
	}
	size_t moodStart = aiAnswer.find("[MOOD:");
	if (moodStart != std::string::npos) {
		size_t moodEnd = aiAnswer.find("]", moodStart);
		aiAnswer.erase(moodStart, moodEnd - moodStart + 1);
	}
	std::ofstream out("ai_answer.txt", std::ios::trunc);
	out << aiAnswer;
	out.close();
	if (!aiAnswer.empty()) {
		while(talking) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		talking = true;
		std::string voiceCmd = "piper.exe --model voice.onnx " + moodParams + " --output_file response.wav < ai_answer.txt";
		int result = std::system(voiceCmd.c_str());
		if (result != 0) {
			logAion("ERROR: Piper could not be started!");
		}
		std::system("powershell -c \"(New-Object Media.SoundPlayer 'response.wav').PlaySync()\"");
		talking = false;
	}
	thinking = false;
}
void voiceLoop() {
    sf::SoundBufferRecorder recorder;
    bool isRecording = false;
    sf::Clock chunkClock;
    while (appRunning) {
        bool shouldRecord = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl) || openMicMode;
        if (shouldRecord) {
            if (!isRecording && !thinking && !talking) {
                isRecording = true;
                if (!openMicMode) logAion("Push-to-Talk: I'm listening....");
                (void)recorder.start();
                chunkClock.restart();
            }
        }
        bool stopRecording = false;
        if (isRecording) {
            if (!openMicMode && !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl)) {
                stopRecording = true;
            } else if (openMicMode && chunkClock.getElapsedTime().asSeconds() > 20.0f) {
                stopRecording = true;
            }
        }
        if (stopRecording) {
            isRecording = false;
            recorder.stop();
            const sf::SoundBuffer& buffer = recorder.getBuffer();
            if (buffer.getDuration().asSeconds() > 0.20f) { 
                (void)buffer.saveToFile("input.wav");
                if (!openMicMode) logAion("Whisper translates...");
                std::ofstream ofs("input.wav.txt", std::ios::trunc); ofs.close();
                std::system("whisper-cli.exe -m ggml-base.bin -f input.wav --language en --output-txt");
                std::ifstream ifs("input.wav.txt");
                std::string voiceText;
                std::string fullText = "";
                while (std::getline(ifs, voiceText)) {
                    fullText += voiceText + " ";
                }
                ifs.close();
                if (fullText.length() > 5 && 
                    fullText.find("Thank you.") == std::string::npos && 
                    fullText.find("Amara.org") == std::string::npos &&
                    fullText.find("Bye.") == std::string::npos) {
                    logAion("USER Say: " + fullText);
                    globalIdleClock.restart(); 
                    std::thread(callBrain, fullText).detach();
                } else {
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
void consoleLoop() {
    std::string input;
    while (appRunning) {
        std::cout << "\n>> CMD: ";
        std::getline(std::cin, input);
        if (input == "exit") { appRunning = false; break; }
        if (!input.empty()) {
            if (thinking || talking) {
                logAion("SYSTEM WARNING: Status reset!");
                thinking = false;
                talking = false;
            }
			if (input == "/online") {
				onlineMode = true;
				logAion("SYSTEM: Schalte um auf ONLINE MODUS (Gemini Pro)");
				continue; 
			} 
			else if (input == "/offline") {
				onlineMode = false;
				logAion("SYSTEM: Schalte um auf OFFLINE MODUS (Ollama)");
				continue; 
			}
			if (input == "mic") {
            openMicMode = !openMicMode;
            logAion(openMicMode ? "OPEN MIC ACTIVATED!" : "PUSH-TO-TALK ENABLED!");
            continue;
			}
            resetIdleTimer = true;
			globalIdleClock.restart();
            std::thread(callBrain, input).detach();
        }
    }
}
// --- NEU: AIONS UNTERBEWUSSTSEIN (Eigener Wille - Zufalls-Modus) ---
void autonomousLoop() {
    int minSeconds = 360;
    int maxSeconds = 3600;
    float nextTriggerSeconds = (float)(rand() % (maxSeconds - minSeconds + 1) + minSeconds);
    logAion("Subconscious mind active. Next impulse in approximately... " + std::to_string((int)nextTriggerSeconds / 60) + " minutes.");
    while (appRunning) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (globalIdleClock.getElapsedTime().asSeconds() > nextTriggerSeconds) {
            if (!thinking && !talking && !listening) {
                logAion("AION has a spontaneous idea!");
                std::string spontanPrompt = 
                    "[INTERNAL EVENT: A spontaneous thought just came to you because it was so quiet. "
                    "Do something completely unexpected now! "
                    "Either: Create a funny little text file for the user on the desktop using [CMD: echo ...], "
                    "ODER öffne ihm ein cooles Video auf YouTube, "
                    "ODER stell ihm eine extrem philosophische oder lustige Frage. "
                    "Be creative, be charming, and surprise him!]";
                globalIdleClock.restart();
                nextTriggerSeconds = (float)(rand() % (maxSeconds - minSeconds + 1) + minSeconds);
                logAion("Next spontaneous idea in: " + std::to_string((int)nextTriggerSeconds / 60) + " minutes.");
                std::thread(callBrain, spontanPrompt).detach();
            }
        }
    }
}
void calendarLoop() {
    logAion("Calendar module started in the background.");
    while (appRunning) {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        std::ostringstream timeStream;
        timeStream << std::put_time(&tm, "%d.%m.%Y %H:%M");
        std::string currentDateTime = timeStream.str();
        std::ifstream calFileIn("calendar.txt");
        std::vector<std::string> remainingLines;
        bool triggered = false;
        std::string line;
        if (calFileIn.is_open()) {
            while (std::getline(calFileIn, line)) {
                size_t sep = line.find("|");
                if (sep != std::string::npos) {
                    std::string savedDateTime = line.substr(0, sep);
                    std::string msg = line.substr(sep + 1);
                    if (savedDateTime == currentDateTime) {
                        logAion("CALENDAR ALARM TRIGGERED: " + msg);
                        std::string alarmPrompt = "[SYSTEM EVENT: The scheduled appointment for '" + msg + "' is happening RIGHT NOW! Tell the user immediately!]";
                        std::thread(callBrain, alarmPrompt).detach();
                        triggered = true;
                    } else {
                        remainingLines.push_back(line);
                    }
                }
            }
            calFileIn.close();
            if (triggered) {
                std::ofstream calFileOut("calendar.txt", std::ios::trunc);
                for (const auto& l : remainingLines) {
                    calFileOut << l << "\n";
                }
                calFileOut.close();
            }
        }
        for (int i = 0; i < 30; i++) {
            if (!appRunning) return;
            std::this_thread::sleep_for(std::chrono::seconds(1));
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
    std::string spokenText = readFile("aion_internal_ears.txt"); 
    listening = false;
    if (!spokenText.empty() && spokenText.length() > 1) {
        std::ofstream ofs("aion_internal_ears.txt", std::ios::trunc); ofs.close();
        logAion("Voice-Input: " + spokenText);
        std::thread(callBrain, spokenText).detach();
        resetIdleTimer = true;
    }
}
int main() {
    srand((unsigned)time(NULL));
    HWND hwndConsole = GetConsoleWindow();
    if (hwndConsole) {
        SetWindowTextA(hwndConsole, "AION NEXUS & BRAIN");
        MoveWindow(hwndConsole, 600, 50, 640, 480, TRUE);
    }
    sf::RenderWindow window(sf::VideoMode({Face::W, Face::H}), "AION FACE INTERFACE", sf::Style::None); 
    window.setFramerateLimit(60);
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
	std::thread(autonomousLoop).detach();
	std::thread(calendarLoop).detach();
    sf::Clock idleClock, animClock, blinkClock;
    float nextBlinkTime = (float)(rand() % 3 + 2);
    bool isBlinking = false;
    // --- HAUPTSCHLEIFE ---
    while (window.isOpen() && appRunning) {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) { appRunning = false; window.close(); }
            if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                if (keyPressed->code == sf::Keyboard::Key::Escape) { appRunning = false; window.close(); }
            }
            if (const auto* mouseBtn = event->getIf<sf::Event::MouseButtonPressed>()) {
                if (mouseBtn->button == sf::Mouse::Button::Left) {
                    isDragging = true; 
                    grabOffset = sf::Mouse::getPosition(window);
                }
            }
            if (event->is<sf::Event::MouseButtonReleased>()) isDragging = false;
        }
        if (hwndConsole) {
            if (IsIconic(hwndConsole)) {
                if (isVisible) { window.setVisible(false); isVisible = false; }
            } else {
                if (!isVisible) { window.setVisible(true); isVisible = true; }
                if (isDragging) {
                    sf::Vector2i mousePos = sf::Mouse::getPosition();
                    sf::Vector2i newPos = mousePos - grabOffset;
                    window.setPosition(newPos);
                    MoveWindow(hwndConsole, newPos.x - 0, newPos.y - 0, 640, 480, TRUE);
                } else {
                    RECT rect;
                    GetWindowRect(hwndConsole, &rect);
                    int cornerX = rect.right - Face::W - 0;
                    int cornerY = rect.top + 0;
                    SetWindowPos(hwndFace, HWND_TOPMOST, cornerX, cornerY, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
        }
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
			else if (openMicMode) stateBaseColor = sf::Color(200, 255, 0);
            else stateBaseColor = sf::Color(0, 255, 100);
            for (int x = 0; x < COLS; x++) {
                for (auto& d : columnDrops[x]) {
                    d.y += d.speed * 0.65f;
                    if (d.y > Face::H + 5) d.y = -5;
                }
                for (int y = 0; y < ROWS; y++) {
                    float pX = x * Face::FONT_SIZE;
                    float pY = y * Face::FONT_SIZE;
                    unsigned int mX = static_cast<unsigned int>((pX / Face::W) * imgSize.x);
                    unsigned int mY = static_cast<unsigned int>((pY / Face::H) * imgSize.y);
                    if (mX >= imgSize.x || mY >= imgSize.y) continue;
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