// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

import QuartzCore
import SwiftUI
import UIKit

final class CitronMetalView: UIView {
    private var touchIds: [ObjectIdentifier: Int] = [:]
    private var nextTouchId = 0

    override class var layerClass: AnyClass {
        CAMetalLayer.self
    }

    private var metalLayer: CAMetalLayer {
        layer as! CAMetalLayer
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        configureLayer()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        configureLayer()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        publishLayer()
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        sendTouches(touches, with: event, phase: .began)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        sendTouches(touches, with: event, phase: .moved)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        sendTouches(touches, with: event, phase: .ended)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        sendTouches(touches, with: event, phase: .ended)
    }

    private func configureLayer() {
        isMultipleTouchEnabled = true
        isOpaque = true
        backgroundColor = .black
        contentScaleFactor = window?.screen.scale ?? UIScreen.main.scale
        metalLayer.isOpaque = true
        metalLayer.pixelFormat = .bgra8Unorm
        metalLayer.framebufferOnly = false
        metalLayer.presentsWithTransaction = false
        metalLayer.contentsScale = window?.screen.scale ?? UIScreen.main.scale
    }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        configureLayer()
        publishLayer()
    }

    private func publishLayer() {
        let scale = window?.screen.scale ?? UIScreen.main.scale
        metalLayer.contentsScale = scale
        metalLayer.drawableSize = CGSize(width: bounds.width * scale, height: bounds.height * scale)
        CitronBridge.setMetalLayer(
            metalLayer,
            width: Int(metalLayer.drawableSize.width),
            height: Int(metalLayer.drawableSize.height),
            scale: scale
        )
    }

    private func sendTouches(_ touches: Set<UITouch>, with event: UIEvent?, phase: UITouch.Phase) {
        for touch in touches {
            let scale = window?.screen.scale ?? UIScreen.main.scale
            let location = touch.location(in: self)
            let x = location.x * scale
            let y = location.y * scale

            switch phase {
            case .began:
                let id = touchId(for: touch)
                CitronBridge.touchBegan(id: id, x: x, y: y)
            case .moved:
                guard let id = touchIds[ObjectIdentifier(touch)] else {
                    continue
                }
                CitronBridge.touchMoved(id: id, x: x, y: y)
            default:
                guard let id = touchIds.removeValue(forKey: ObjectIdentifier(touch)) else {
                    continue
                }
                CitronBridge.touchEnded(id: id)
            }
        }
    }

    private func touchId(for touch: UITouch) -> Int {
        let key = ObjectIdentifier(touch)
        if let existing = touchIds[key] {
            return existing
        }
        let id = nextTouchId
        nextTouchId += 1
        touchIds[key] = id
        return id
    }
}

struct RenderView: UIViewRepresentable {
    func makeUIView(context: Context) -> CitronMetalView {
        let view = CitronMetalView()
        DispatchQueue.main.async {
            view.setNeedsLayout()
            view.layoutIfNeeded()
        }
        return view
    }

    func updateUIView(_ uiView: CitronMetalView, context: Context) {}
}
