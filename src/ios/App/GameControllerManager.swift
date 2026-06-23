// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import Foundation
import GameController

final class GameControllerManager {
    static let shared = GameControllerManager()

    private var isStarted = false

    private init() {}

    func start() {
        guard !isStarted else {
            return
        }
        isStarted = true

        NotificationCenter.default.addObserver(
            self,
            selector: #selector(controllerDidConnect(_:)),
            name: .GCControllerDidConnect,
            object: nil
        )

        for controller in GCController.controllers() {
            configure(controller)
        }

        GCController.startWirelessControllerDiscovery {}
    }

    @objc private func controllerDidConnect(_ notification: Notification) {
        guard let controller = notification.object as? GCController else {
            return
        }
        configure(controller)
    }

    private func configure(_ controller: GCController) {
        guard let gamepad = controller.extendedGamepad else {
            return
        }

        gamepad.valueChangedHandler = { gamepad, element in
            Self.handle(gamepad: gamepad, changedElement: element)
        }
    }

    private static func handle(gamepad: GCExtendedGamepad, changedElement element: GCControllerElement) {
        sendButton(gamepad.buttonA, element, id: 0)
        sendButton(gamepad.buttonB, element, id: 1)
        sendButton(gamepad.buttonX, element, id: 2)
        sendButton(gamepad.buttonY, element, id: 3)
        sendButton(gamepad.leftThumbstickButton, element, id: 4)
        sendButton(gamepad.rightThumbstickButton, element, id: 5)
        sendButton(gamepad.leftShoulder, element, id: 6)
        sendButton(gamepad.rightShoulder, element, id: 7)
        sendButton(gamepad.leftTrigger, element, id: 8)
        sendButton(gamepad.rightTrigger, element, id: 9)
        sendButton(gamepad.buttonMenu, element, id: 10)
        sendDirection(gamepad.dpad.left, element, id: 12)
        sendDirection(gamepad.dpad.up, element, id: 13)
        sendDirection(gamepad.dpad.right, element, id: 14)
        sendDirection(gamepad.dpad.down, element, id: 15)

        if element == gamepad.leftThumbstick {
            CitronBridge.setStick(stickId: 0, x: gamepad.leftThumbstick.xAxis.value, y: gamepad.leftThumbstick.yAxis.value)
        }

        if element == gamepad.rightThumbstick {
            CitronBridge.setStick(stickId: 1, x: gamepad.rightThumbstick.xAxis.value, y: gamepad.rightThumbstick.yAxis.value)
        }
    }

    private static func sendButton(_ button: GCControllerButtonInput?, _ element: GCControllerElement, id: Int) {
        guard let button, element == button else {
            return
        }
        CitronBridge.setButton(buttonId: id, pressed: button.isPressed)
    }

    private static func sendDirection(_ button: GCControllerButtonInput, _ element: GCControllerElement, id: Int) {
        guard element == button else {
            return
        }
        CitronBridge.setButton(buttonId: id, pressed: button.isPressed)
    }
}
