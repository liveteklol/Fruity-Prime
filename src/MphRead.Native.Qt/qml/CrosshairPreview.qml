import QtQuick

// CrosshairPreview: the HUD's crosshair at its own size on a panel square.
Canvas {
    id: preview
    property var shape: ({ bars: [], radius: 0, thickness: 0 })
    onShapeChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.fillStyle = Theme.panel
        c.strokeStyle = Theme.edge
        c.lineWidth = 1
        c.beginPath()
        c.roundedRect(0.5, 0.5, width - 1, height - 1, 4, 4)
        c.fill()
        c.stroke()
        const cx = width / 2, cy = height / 2
        c.fillStyle = Theme.text
        for (const b of shape.bars)
            c.fillRect(cx + b[0], cy + b[1], b[2], b[3])
        if (shape.thickness > 0) {
            c.strokeStyle = Theme.text
            c.lineWidth = shape.thickness
            c.beginPath()
            c.arc(cx, cy, shape.radius, 0, Math.PI * 2)
            c.stroke()
        }
    }
}
