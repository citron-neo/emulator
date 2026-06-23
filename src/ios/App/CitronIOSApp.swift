// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import SwiftUI
import AVFAudio

@main
struct CitronIOSApp: App {
    init() {
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            try session.setActive(true)
        } catch {
            print("Failed to configure audio session: \(error)")
        }

        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        CitronBridge.initialize(appDirectory: documents.path)
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}
