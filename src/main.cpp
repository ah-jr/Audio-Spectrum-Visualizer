// Main application - DO NOT include Windows headers here (conflicts with raylib)

#include "audio_analyzer.hpp"
#include "spectrum_visualizer.hpp"
#include "file_dialog.hpp"

#include <iostream>
#include <string>

void printUsage(const char* programName) {
    std::cout << "\n";
    std::cout << "======================================================================\n";
    std::cout << "           AUDIO SPECTRUM VISUALIZER + 5-BAND EQUALIZER               \n";
    std::cout << "           Real-time FFT Visualization (20Hz - 20kHz)                 \n";
    std::cout << "======================================================================\n";
    std::cout << "\n";
    std::cout << "Usage: " << programName << " [audio_file]\n";
    std::cout << "\n";
    std::cout << "Supported formats: MP3, WAV, FLAC, OGG, M4A, AAC\n";
    std::cout << "\n";
    std::cout << "Playback Controls:\n";
    std::cout << "  SPACE      - Play / Pause\n";
    std::cout << "  LEFT/RIGHT - Seek backward / forward (5 seconds)\n";
    std::cout << "  O          - Open file dialog\n";
    std::cout << "\n";
    std::cout << "Visualization:\n";
    std::cout << "  S          - Change visualization style\n";
    std::cout << "  T          - Change color theme\n";
    std::cout << "  G          - Toggle frequency grid\n";
    std::cout << "  P          - Toggle peak indicators\n";
    std::cout << "  I          - Toggle info display\n";
    std::cout << "\n";
    std::cout << "Equalizer (Line mode):\n";
    std::cout << "  E          - Toggle EQ on/off\n";
    std::cout << "  R          - Reset EQ to flat\n";
    std::cout << "  Drag knobs - Adjust frequency bands (+/- 12dB)\n";
    std::cout << "  Bands: 60Hz, 250Hz, 1kHz, 4kHz, 12kHz\n";
    std::cout << "\n";
    std::cout << "  ESC        - Exit\n";
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    printUsage(argv[0]);
    
    // Initialize audio analyzer
    audio::AudioAnalyzer analyzer;
    
    // Configure analyzer
    audio::AnalyzerConfig analyzerConfig;
    analyzerConfig.fftSize = 8192;        // Larger FFT for better low-freq resolution
    analyzerConfig.numBands = 256;         // More bands for smoother display
    analyzerConfig.minFrequency = 20.0;
    analyzerConfig.maxFrequency = 20000.0;
    analyzerConfig.smoothingFactor = 0.6;  // Less smoothing for more responsive display
    analyzerConfig.useLogScale = true;
    
    analyzer.setConfig(analyzerConfig);
    
    if (!analyzer.initialize()) {
        std::cerr << "Failed to initialize audio analyzer!\n";
        return 1;
    }
    
    // Initialize visualizer
    viz::SpectrumVisualizer visualizer;
    
    viz::VisualizerConfig vizConfig;
    vizConfig.windowWidth = 1280;
    vizConfig.windowHeight = 720;
    vizConfig.targetFps = 60;
    vizConfig.style = viz::VisualizerStyle::Line;  // Default to line style
    vizConfig.theme = viz::ColorTheme::Sunset();   
    vizConfig.sensitivity = 1.0f;                   // Proper dB scaling
    vizConfig.showPeaks = true;
    vizConfig.showGrid = true;
    vizConfig.showInfo = true;
    
    if (!visualizer.initialize(vizConfig)) {
        std::cerr << "Failed to initialize visualizer!\n";
        return 1;
    }
    
    // Load audio file if provided
    std::string audioFile;
    
    if (argc > 1) {
        audioFile = argv[1];
    }
    
    if (!audioFile.empty()) {
        std::cout << "Loading: " << audioFile << "\n";
        if (analyzer.loadFile(audioFile)) {
            std::cout << "Loaded successfully!\n";
            std::cout << "Sample rate: " << analyzer.getSampleRate() << " Hz\n";
            std::cout << "Duration: " << analyzer.getDuration() << " seconds\n";
            analyzer.play();
        } else {
            std::cerr << "Failed to load audio file: " << audioFile << "\n";
        }
    } else {
        std::cout << "No audio file specified. Press 'O' to open a file.\n";
    }
    
    // Main loop
    while (!visualizer.shouldClose()) {
        // Handle 'O' key for open file dialog
        if (IsKeyPressed(KEY_O)) {
            std::string newFile = util::openFileDialog();
            if (!newFile.empty()) {
                std::cout << "Loading: " << newFile << "\n";
                if (analyzer.loadFile(newFile)) {
                    std::cout << "Loaded successfully!\n";
                    analyzer.play();
                } else {
                    std::cerr << "Failed to load: " << newFile << "\n";
                }
            }
        }
        
        // Handle dropped files
        if (IsFileDropped()) {
            FilePathList droppedFiles = LoadDroppedFiles();
            if (droppedFiles.count > 0) {
                std::string droppedFile = droppedFiles.paths[0];
                std::cout << "Loading dropped file: " << droppedFile << "\n";
                if (analyzer.loadFile(droppedFile)) {
                    std::cout << "Loaded successfully!\n";
                    analyzer.play();
                } else {
                    std::cerr << "Failed to load: " << droppedFile << "\n";
                }
            }
            UnloadDroppedFiles(droppedFiles);
        }
        
        // Handle input
        visualizer.handleInput(analyzer);
        
        // Get spectrum data
        audio::SpectrumData spectrum = analyzer.getSpectrum();
        
        // Render
        visualizer.render(spectrum, analyzer);
    }
    
    std::cout << "Goodbye!\n";
    return 0;
}
