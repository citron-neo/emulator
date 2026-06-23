// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import Foundation

typealias CitronLifecycleCallback = @convention(c) (Int32) -> Void

@_silgen_name("citron_ios_initialize")
private func citron_ios_initialize(_ appDirectory: UnsafePointer<CChar>?)

@_silgen_name("citron_ios_set_callbacks")
private func citron_ios_set_callbacks(_ started: CitronLifecycleCallback?,
                                      _ stopped: CitronLifecycleCallback?)

@_silgen_name("citron_ios_set_metal_layer")
private func citron_ios_set_metal_layer(_ metalLayer: UnsafeMutableRawPointer?,
                                        _ width: Int32,
                                        _ height: Int32,
                                        _ scale: Float)

@_silgen_name("citron_ios_launch_game")
private func citron_ios_launch_game(_ filepath: UnsafePointer<CChar>?, _ programIndex: Int32) -> Int32

@_silgen_name("citron_ios_pause")
private func citron_ios_pause()

@_silgen_name("citron_ios_resume")
private func citron_ios_resume()

@_silgen_name("citron_ios_stop")
private func citron_ios_stop()

@_silgen_name("citron_ios_shutdown")
private func citron_ios_shutdown()

@_silgen_name("citron_ios_is_running")
private func citron_ios_is_running() -> Bool

@_silgen_name("citron_ios_is_paused")
private func citron_ios_is_paused() -> Bool

@_silgen_name("citron_ios_touch_began")
private func citron_ios_touch_began(_ id: Int32, _ x: Float, _ y: Float)

@_silgen_name("citron_ios_touch_moved")
private func citron_ios_touch_moved(_ id: Int32, _ x: Float, _ y: Float)

@_silgen_name("citron_ios_touch_ended")
private func citron_ios_touch_ended(_ id: Int32)

@_silgen_name("citron_ios_set_button")
private func citron_ios_set_button(_ playerIndex: Int32, _ buttonId: Int32, _ pressed: Bool)

@_silgen_name("citron_ios_set_stick")
private func citron_ios_set_stick(_ playerIndex: Int32, _ stickId: Int32, _ x: Float, _ y: Float)

@_silgen_name("citron_ios_jit_available")
private func citron_ios_jit_available() -> Bool

enum CitronBridge {
    static func initialize(appDirectory: String) {
        appDirectory.withCString { citron_ios_initialize($0) }
        citron_ios_set_callbacks(citronStarted, citronStopped)
    }

    static func setMetalLayer(_ layer: AnyObject, width: Int, height: Int, scale: CGFloat) {
        let pointer = Unmanaged.passUnretained(layer).toOpaque()
        citron_ios_set_metal_layer(pointer, Int32(width), Int32(height), Float(scale))
    }

    @discardableResult
    static func launchGame(path: String) -> Int32 {
        path.withCString { citron_ios_launch_game($0, 0) }
    }

    static func pause() {
        citron_ios_pause()
    }

    static func resume() {
        citron_ios_resume()
    }

    static func stop() {
        citron_ios_stop()
    }

    static func shutdown() {
        citron_ios_shutdown()
    }

    static var isRunning: Bool {
        citron_ios_is_running()
    }

    static var isPaused: Bool {
        citron_ios_is_paused()
    }

    static var isJITAvailable: Bool {
        citron_ios_jit_available()
    }

    static func touchBegan(id: Int, x: CGFloat, y: CGFloat) {
        citron_ios_touch_began(Int32(id), Float(x), Float(y))
    }

    static func touchMoved(id: Int, x: CGFloat, y: CGFloat) {
        citron_ios_touch_moved(Int32(id), Float(x), Float(y))
    }

    static func touchEnded(id: Int) {
        citron_ios_touch_ended(Int32(id))
    }

    static func setButton(playerIndex: Int = 0, buttonId: Int, pressed: Bool) {
        citron_ios_set_button(Int32(playerIndex), Int32(buttonId), pressed)
    }

    static func setStick(playerIndex: Int = 0, stickId: Int, x: Float, y: Float) {
        citron_ios_set_stick(Int32(playerIndex), Int32(stickId), x, y)
    }
}

private func citronStarted(_ result: Int32) {
    NotificationCenter.default.post(name: .citronStarted, object: result)
}

private func citronStopped(_ result: Int32) {
    NotificationCenter.default.post(name: .citronStopped, object: result)
}

extension Notification.Name {
    static let citronStarted = Notification.Name("CitronStarted")
    static let citronStopped = Notification.Name("CitronStopped")
}
