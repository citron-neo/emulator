// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import SwiftUI
import UniformTypeIdentifiers
import UIKit

final class CitronAppState: ObservableObject {
    @Published var selectedGameURL: URL?
    @Published var localGameURLs: [URL] = []
    @Published var statusText = "Select a game to begin."
    @Published var isRunning = false
    @Published var isPaused = false
    @Published var isShowingImporter = false

    private var securityScopedURL: URL?
    private let gameExtensions = Set(["xci", "nsp", "nca", "nro", "nso"])

    init() {
        NotificationCenter.default.addObserver(
            forName: .citronStarted,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            self?.isRunning = true
            self?.isPaused = false
            self?.statusText = "Emulation started (\(notification.object ?? 0))."
        }

        NotificationCenter.default.addObserver(
            forName: .citronStopped,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            self?.isRunning = false
            self?.isPaused = false
            self?.stopAccessingGame()
            self?.statusText = "Emulation stopped (\(notification.object ?? 0))."
        }
    }

    func selectGame(_ url: URL) {
        stopAccessingGame()
        selectedGameURL = url
        statusText = "Selected \(url.lastPathComponent)."
    }

    func refreshLocalGames() {
        guard let documentsURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else {
            statusText = "Documents directory is unavailable."
            return
        }

        let keys: [URLResourceKey] = [.isRegularFileKey]
        let enumerator = FileManager.default.enumerator(
            at: documentsURL,
            includingPropertiesForKeys: keys,
            options: [.skipsHiddenFiles]
        )

        localGameURLs = (enumerator?.compactMap { item -> URL? in
            guard let url = item as? URL else {
                return nil
            }

            let values = try? url.resourceValues(forKeys: Set(keys))
            guard values?.isRegularFile == true else {
                return nil
            }

            return gameExtensions.contains(url.pathExtension.lowercased()) ? url : nil
        } ?? [])
        .sorted { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }

        if localGameURLs.isEmpty {
            statusText = "No games found in Documents. Copy .nsp/.xci files into the app container."
        } else {
            statusText = "Found \(localGameURLs.count) local game(s)."
        }
    }

    func launchSelectedGame() {
        guard let selectedGameURL else {
            statusText = "No game selected."
            return
        }

        if !CitronBridge.isJITAvailable {
            statusText = "JIT is not currently available. Enable JIT for this sideloaded app first."
            return
        }

        let didStartAccess = selectedGameURL.startAccessingSecurityScopedResource()
        let result = CitronBridge.launchGame(path: selectedGameURL.path)
        if didStartAccess {
            selectedGameURL.stopAccessingSecurityScopedResource()
        }
        statusText = launchStatusMessage(result)
        isRunning = result == 0
    }

    func togglePause() {
        if isPaused {
            CitronBridge.resume()
            isPaused = false
            statusText = "Resumed."
        } else {
            CitronBridge.pause()
            isPaused = true
            statusText = "Paused."
        }
    }

    func stop() {
        CitronBridge.stop()
        stopAccessingGame()
        isRunning = false
        isPaused = false
        statusText = "Stopped."
    }

    private func stopAccessingGame() {
        securityScopedURL?.stopAccessingSecurityScopedResource()
        securityScopedURL = nil
    }

    private func launchStatusMessage(_ result: Int32) -> String {
        switch result {
        case 0:
            return "Launch started."
        case 1:
            return "Launch failed: core was not initialized."
        case 2:
            return "Launch failed: no compatible game loader was found."
        case 3:
            return "Launch failed: required system files are missing."
        case 4:
            return "Launch failed: shared font data is missing."
        case 5:
            return "Launch failed: video/GPU initialization failed. Check Documents/citron/log/citron_log.txt."
        case 19:
            return "Launch failed: bad NCA header. Copy prod.keys and title.keys into Documents/citron/keys."
        case 30:
            return "Launch failed: no iOS JIT API linked (pthread_jit_write_with_callback_np). Needs iOS 17.4+ and correct embedded library signing."
        default:
            return "Launch returned \(result)."
        }
    }
}

struct ContentView: View {
    @StateObject private var state = CitronAppState()
    private let gameControllerManager = GameControllerManager.shared

    var body: some View {
        ZStack(alignment: .top) {
            RenderView()
                .ignoresSafeArea()

            VStack(spacing: 12) {
                Text(state.statusText)
                    .font(.footnote)
                    .foregroundStyle(.white)
                    .padding(8)
                    .background(.black.opacity(0.65), in: Capsule())

                HStack {
                    Button("Choose Game") {
                        state.isShowingImporter = true
                    }

                    Menu("Local Games") {
                        if state.localGameURLs.isEmpty {
                            Button("Scan Documents") {
                                state.refreshLocalGames()
                            }
                        } else {
                            ForEach(state.localGameURLs, id: \.self) { url in
                                Button(url.lastPathComponent) {
                                    state.selectGame(url)
                                }
                            }

                            Button("Rescan Documents") {
                                state.refreshLocalGames()
                            }
                        }
                    }

                    Button(state.isRunning ? "Restart" : "Launch") {
                        state.launchSelectedGame()
                    }
                    .disabled(state.selectedGameURL == nil)

                    Button(state.isPaused ? "Resume" : "Pause") {
                        state.togglePause()
                    }
                    .disabled(!state.isRunning)

                    Button("Stop") {
                        state.stop()
                    }
                    .disabled(!state.isRunning)
                }
                .buttonStyle(.borderedProminent)
            }
            .padding(.top, 24)
        }
        .sheet(isPresented: $state.isShowingImporter) {
            GameDocumentPicker { url in
                if let url {
                    state.selectGame(url)
                } else {
                    state.statusText = "Game selection cancelled."
                }
                state.isShowingImporter = false
            }
        }
        .onAppear {
            gameControllerManager.start()
            state.refreshLocalGames()
        }
    }
}

struct GameDocumentPicker: UIViewControllerRepresentable {
    let onPick: (URL?) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(onPick: onPick)
    }

    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let contentTypes = ["xci", "nsp", "nca", "nro", "nso"].compactMap { UTType(filenameExtension: $0) }
        let picker = UIDocumentPickerViewController(
            forOpeningContentTypes: contentTypes.isEmpty ? [.data] : contentTypes,
            asCopy: true
        )
        picker.allowsMultipleSelection = false
        picker.delegate = context.coordinator
        return picker
    }

    func updateUIViewController(_ uiViewController: UIDocumentPickerViewController, context: Context) {}

    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        private let onPick: (URL?) -> Void

        init(onPick: @escaping (URL?) -> Void) {
            self.onPick = onPick
        }

        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
            onPick(urls.first)
        }

        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
            onPick(nil)
        }
    }
}
