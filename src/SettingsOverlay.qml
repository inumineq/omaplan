import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts


// The settings (, or Ctrl+,): the AI engines, how dates and times are read
// and shown, and the project colours. config.toml stays the only source: every
// change is written there at once (actions.guiConfigSet, which keeps comments
// and unknown keys), and the window follows the file, so a change made by
// `omaplan config set` or in an editor shows here too. Tab walks the fields,
// ← / → change a choice, Alt+1/2/3 pick the section, Ctrl+E opens the file,
// Esc closes.
Rectangle {
    id: panel
    objectName: "settingsPanel"
    color: Qt.alpha(theme.background, 0.82) // a scrim in the theme's own colour, light or dark
    visible: false

    property string section: "engine"   // engine | dates | projects
    property string problem: ""
    property var engineInfo: ({ "engines": [] })
    property var projectNames: []
    // engine id -> { running, ok, message, ms }
    property var tests: ({})
    // True while closing: a field that loses the focus then saves nothing.
    property bool closing: false
    readonly property var engineIds: ["vibe", "claude", "local", "command"]

    signal closeRequested()

    function open() {
        problem = ""
        refresh()
        flick.contentY = 0
        visible = true
        sectionChoice.forceActiveFocus()
    }
    // Closes without saving a half-typed field, and stops a running Test.
    function close() {
        closing = true
        const f = Window.activeFocusItem as ConfigInput
        if (f && f.configField)
            f.text = f.configField.stored
        for (const id in tests)
            if (tests[id].running)
                cancelTest(id)
        visible = false
        closing = false
    }
    // Esc: first undoes a field's unsaved typing, then closes.
    function handleEscape() {
        const f = Window.activeFocusItem as ConfigInput
        if (f && f.configField && f.text !== f.configField.stored) {
            f.text = f.configField.stored
            // A field in another section is not seen being restored: close too.
            if (f.visible)
                return
        }
        closeRequested()
    }
    // Only real changes are assigned: a new array would rebuild the
    // Repeaters and drop the focus.
    function refresh() {
        const info = actions.guiEngines()
        if (JSON.stringify(info) !== JSON.stringify(engineInfo))
            engineInfo = info
        const r = actions.guiProjects()
        const names = r.ok ? r.payload.projects.map(p => p.name) : []
        for (const name of Object.keys(settings.projectColors))
            if (names.indexOf(name) < 0)
                names.push(name)
        names.sort((a, b) => a.localeCompare(b))
        if (JSON.stringify(names) !== JSON.stringify(projectNames))
            projectNames = names
    }
    function engine(id) { return engineInfo.engines.find(e => e.id === id) || ({ "id": id, "name": id }) }
    // Writes one key; an empty value removes it, so the default applies.
    function set(key, value) {
        const r = actions.guiConfigSet(key, value)
        problem = r.ok ? "" : r.error
        // At once, not after the file watcher's delay: a second → reads the new value.
        if (r.ok)
            settings.reload()
        return r.ok
    }
    // A language in its own words; "en" alone would read "American English".
    function languageName(code) {
        return code === "en" ? "English" : Fmt.capitalize(Qt.locale(code).nativeLanguageName)
    }
    function value(key) { return settings.values[key] || "" }
    function choice(key) { return settings.values[key] || "auto" }

    // Tab may move the focus below the fold: scroll it into view.
    readonly property Item focusItem: Window.activeFocusItem
    onFocusItemChanged: {
        let inside = false
        for (let p = focusItem; p; p = p.parent)
            if (p === content) { inside = true; break }
        if (!visible || !inside)
            return
        const y = focusItem.mapToItem(content, 0, 0).y
        if (y < flick.contentY)
            flick.contentY = Math.max(0, y - 12)
        else if (y + focusItem.height > flick.contentY + flick.height)
            flick.contentY = Math.min(content.implicitHeight - flick.height, y + focusItem.height - flick.height + 12)
    }

    Connections {
        target: settings
        function onChanged() { if (panel.visible) panel.refresh() }
    }
    Connections {
        target: actions
        function onEngineTested(id, ok, message, ms) {
            const t = Object.assign({}, panel.tests)
            t[id] = { "running": false, "ok": ok, "message": message, "ms": ms }
            panel.tests = t
        }
    }
    function test(id) {
        const t = Object.assign({}, tests)
        t[id] = { "running": true, "ok": false, "message": "", "ms": 0 }
        tests = t
        actions.guiTestEngine(id)
    }
    function cancelTest(id) {
        actions.guiCancelTest(id)
        const t = Object.assign({}, tests)
        t[id] = { "running": false, "ok": false, "message": qsTr("Cancelled."), "ms": 0 }
        tests = t
    }

    Shortcut { sequence: "Esc"; enabled: panel.visible; onActivated: panel.handleEscape() }
    Shortcut { sequence: "Alt+1"; enabled: panel.visible; onActivated: panel.section = "engine" }
    Shortcut { sequence: "Alt+2"; enabled: panel.visible; onActivated: panel.section = "dates" }
    Shortcut { sequence: "Alt+3"; enabled: panel.visible; onActivated: panel.section = "projects" }
    Shortcut {
        sequence: "Ctrl+E"
        enabled: panel.visible
        onActivated: if (!settings.openInEditor()) panel.problem = qsTr("Could not open %1").arg(settings.path)
    }

    MouseArea { anchors.fill: parent; onClicked: panel.closeRequested() }

    // A small capital heading, as in the shortcut overview.
    component Heading: Text {
        font.family: theme.fontFamily
        Layout.topMargin: 10
        color: theme.muted
        font.weight: Font.DemiBold
        font.pixelSize: Math.round(11 * theme.textScale)
        font.letterSpacing: 1.2
    }
    component Note: Text {
        font.family: theme.fontFamily
        Layout.fillWidth: true
        color: theme.muted
        font.pixelSize: Math.round(11 * theme.textScale)
        wrapMode: Text.Wrap
    }
    // The text field of a saving ConfigField knows it, so the panel can restore
    // the focused field's stored value.
    component ConfigInput: TextField {
        property ConfigField configField: null
    }
    // A text setting: shows the file's value, writes it on Enter or when the
    // field loses focus.
    component ConfigField: ColumnLayout {
        id: fieldBox
        property string key
        property string label
        property string placeholder: ""
        property bool saves: true          // false: a scratch field (the date preview)
        property alias input: textField
        readonly property string stored: saves ? panel.value(key) : ""
        onStoredChanged: if (!textField.activeFocus) textField.text = stored
        Component.onCompleted: textField.text = stored
        spacing: 3
        Text {
            font.family: theme.fontFamily
            Layout.fillWidth: true // wraps rather than widen the field past the panel
            wrapMode: Text.Wrap
            text: fieldBox.label
            color: theme.muted
            font.pixelSize: Math.round(11 * theme.textScale)
        }
        ConfigInput {
            id: textField
            // Each engine card holds every kind of field; only the shown one is named.
            objectName: fieldBox.saves && fieldBox.visible ? "field:" + fieldBox.key : ""
            configField: fieldBox.saves ? fieldBox : null
            Layout.fillWidth: true
            selectedTextColor: theme.accentForeground
            font.pixelSize: Math.round(13 * theme.textScale)
            color: theme.foreground
            placeholderText: text === "" ? fieldBox.placeholder : ""
            placeholderTextColor: theme.muted
            selectByMouse: true
            leftPadding: 8
            topPadding: 5
            bottomPadding: 5
            background: Rectangle {
                radius: Math.min(3, theme.radius)
                color: theme.background
                border.color: textField.activeFocus ? theme.accent : theme.border
            }
            // A rejected value goes back to the stored one; the error stays shown.
            onEditingFinished: {
                if (!fieldBox.saves || panel.closing || text.trim() === fieldBox.stored)
                    return
                if (!panel.set(fieldBox.key, text.trim()))
                    text = fieldBox.stored
            }
        }
    }
    // A choice setting as a row of buttons.
    component ConfigChoice: Segmented {
        property string key
        focusable: true
        keyInline: true
        maxWidth: flick.width
        value: panel.choice(key)
        onPicked: v => panel.set(key, v === "auto" ? "" : v)
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(780, parent.width - 48)
        height: Math.min(content.implicitHeight + 40, parent.height - 48)
        radius: Math.min(8, theme.radius)
        color: theme.surface
        border.color: theme.accent
        MouseArea { anchors.fill: parent }

        Flickable {
            id: flick
            objectName: "settingsFlick"
            anchors.fill: parent
            anchors.margins: 20
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: flick.width
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        font.family: theme.fontFamily
                        text: qsTr("Settings")
                        font.weight: Font.DemiBold
                        font.pixelSize: Math.round(16 * theme.textScale)
                        color: theme.foreground
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        font.family: theme.fontFamily
                        text: qsTr("Esc closes")
                        color: theme.muted
                        font.pixelSize: Math.round(12 * theme.textScale)
                    }
                }
                Segmented {
                    id: sectionChoice
                    focusable: true
                    keyInline: true
                    maxWidth: flick.width
                    keyHint: "Alt+1/2/3"
                    options: [["engine", qsTr("AI engine")], ["dates", qsTr("Date and time")],
                              ["projects", qsTr("Project colours")]]
                    value: panel.section
                    onPicked: v => panel.section = v
                }

                // --- AI engine -----------------------------------------------
                ColumnLayout {
                    visible: panel.section === "engine"
                    Layout.fillWidth: true
                    spacing: 8

                    Heading { text: qsTr("Default engine").toUpperCase() }
                    ConfigChoice {
                        key: "engine.default"
                        options: panel.engineInfo.engines.map(e => [e.id, e.name])
                        value: panel.engineInfo.default || "vibe"
                    }
                    Repeater {
                        // A fixed list: a saved field updates the card's
                        // bindings instead of rebuilding it (and its focus).
                        model: panel.engineIds
                        delegate: Rectangle {
                            id: card
                            required property string modelData
                            readonly property var info: panel.engine(modelData)
                            readonly property var test: panel.tests[modelData]
                            Layout.fillWidth: true
                            implicitHeight: cardBody.implicitHeight + 20
                            radius: Math.min(4, theme.radius)
                            color: theme.background
                            border.color: card.info.default ? theme.accent : theme.border

                            ColumnLayout {
                                id: cardBody
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 10
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Rectangle {
                                        implicitWidth: 8; implicitHeight: 8; radius: 4
                                        color: card.info.ready ? theme.success : theme.danger
                                    }
                                    Text {
                                        font.family: theme.fontFamily
                                        text: card.info.name
                                        color: theme.foreground
                                        font.pixelSize: Math.round(13 * theme.textScale)
                                        font.bold: true
                                    }
                                    Text {
                                        font.family: theme.fontFamily
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                        text: card.info.ready
                                              ? (card.info.path ? qsTr("found: %1").arg(card.info.path) : qsTr("ready"))
                                              : (card.info.problem || "")
                                        color: card.info.ready ? theme.muted : theme.danger
                                        font.pixelSize: Math.round(11 * theme.textScale)
                                    }
                                    HeaderButton {
                                        objectName: "test:" + card.modelData
                                        readonly property bool running: !!card.test && card.test.running
                                        text: running ? qsTr("Stop") : qsTr("Test")
                                        focusable: true
                                        toolTip: running ? qsTr("Stop the test") : qsTr("Send one short request to %1").arg(card.info.name)
                                        onClicked: running ? panel.cancelTest(card.modelData) : panel.test(card.modelData)
                                    }
                                }
                                // vibe and claude: the model; local: API, endpoint
                                // and model; command: the command line.
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    ConfigChoice {
                                        visible: card.modelData === "local"
                                        Layout.alignment: Qt.AlignBottom
                                        key: "engine.local.api"
                                        label: qsTr("API")
                                        options: [["ollama", "Ollama"], ["openai", qsTr("OpenAI-compatible")]]
                                        value: card.info.api || "ollama"
                                    }
                                    ConfigField {
                                        visible: card.modelData === "local"
                                        Layout.fillWidth: true
                                        key: "engine.local.endpoint"
                                        label: qsTr("Endpoint")
                                        placeholder: "http://127.0.0.1:11434"
                                    }
                                    ConfigField {
                                        visible: card.modelData !== "command"
                                        Layout.fillWidth: true
                                        key: "engine." + card.modelData + ".model"
                                        label: qsTr("Model")
                                        placeholder: card.modelData === "local" ? "llama3.2" : qsTr("the tool's default")
                                    }
                                    ConfigField {
                                        visible: card.modelData === "command"
                                        Layout.fillWidth: true
                                        key: "engine.command"
                                        label: qsTr("Command line (prompt on stdin, JSON on stdout)")
                                        placeholder: "my-engine --json"
                                    }
                                }
                                Text {
                                    font.family: theme.fontFamily
                                    Layout.fillWidth: true
                                    visible: !!card.test && !card.test.running
                                    wrapMode: Text.Wrap
                                    font.pixelSize: Math.round(12 * theme.textScale)
                                    color: card.test && card.test.ok ? theme.foreground : theme.danger
                                    text: !card.test ? "" : card.test.ms > 0
                                          ? qsTr("%1 (%2 s)").arg(card.test.message).arg((card.test.ms / 1000).toFixed(1))
                                          : card.test.message
                                }
                            }
                        }
                    }
                    Heading { text: qsTr("Compare (Alt+S)").toUpperCase() }
                    ColumnLayout {
                        spacing: 4
                        Segmented {
                            focusable: true
                            maxWidth: flick.width
                            label: qsTr("Left")
                            options: panel.engineInfo.engines.map(e => [e.id, e.name])
                            value: (panel.engineInfo.compare || [])[0] || ""
                            onPicked: v => panel.set("engine.compare", v + ", " + ((panel.engineInfo.compare || [])[1] || ""))
                        }
                        Segmented {
                            focusable: true
                            maxWidth: flick.width
                            label: qsTr("Right")
                            options: panel.engineInfo.engines.map(e => [e.id, e.name])
                            value: (panel.engineInfo.compare || [])[1] || ""
                            onPicked: v => panel.set("engine.compare", ((panel.engineInfo.compare || [])[0] || "") + ", " + v)
                        }
                    }
                    ConfigField {
                        Layout.preferredWidth: 220
                        key: "engine.timeout"
                        label: qsTr("Timeout in seconds (10–600)")
                        placeholder: "90"
                    }
                    Note { text: qsTr("Test sends one short request and stores nothing. Omaplan never stores API keys; the tools keep their own login.") }
                }

                // --- Date and time -------------------------------------------
                ColumnLayout {
                    visible: panel.section === "dates"
                    Layout.fillWidth: true
                    spacing: 6

                    Heading { text: qsTr("Language").toUpperCase() }
                    ConfigChoice {
                        key: "language"
                        options: [["auto", qsTr("Automatic")]].concat(
                                     settings.languages.map(c => [c, panel.languageName(c)]))
                    }
                    Heading { text: qsTr("Reading dates").toUpperCase() }
                    ConfigChoice {
                        key: "dates.order"
                        label: qsTr("12/11 is")
                        options: [["auto", qsTr("Automatic")], ["dmy", qsTr("Day/month")], ["mdy", qsTr("Month/day")]]
                    }
                    ConfigChoice {
                        key: "dates.dotted"
                        label: qsTr("12.12 is")
                        options: [["auto", qsTr("Automatic")], ["time", qsTr("A time")], ["date", qsTr("A date")]]
                    }
                    Heading { text: qsTr("Showing dates").toUpperCase() }
                    ConfigChoice {
                        key: "dates.week_start"
                        label: qsTr("Week starts")
                        options: [["auto", qsTr("Automatic")], ["monday", qsTr("Monday")], ["sunday", qsTr("Sunday")]]
                    }
                    ConfigChoice {
                        key: "dates.clock"
                        label: qsTr("Clock")
                        options: [["auto", qsTr("Automatic")], ["24h", qsTr("24-hour")], ["12h", qsTr("12-hour")]]
                    }
                    Note {
                        text: qsTr("Now: %1 · 12.12 is %2 · the week starts on %3 · %4. Automatic follows the language.")
                              .arg(settings.dayFirst ? qsTr("day/month") : qsTr("month/day"))
                              .arg(settings.dottedIsTime ? qsTr("a time") : qsTr("a date"))
                              .arg(Fmt.locale.dayName(settings.weekStart === 7 ? 0 : 1, Locale.LongFormat))
                              .arg(settings.clock24 ? qsTr("24-hour clock") : qsTr("12-hour clock"))
                    }
                    Heading { text: qsTr("Preview").toUpperCase() }
                    Repeater {
                        model: ["12/11", "12.12", qsTr("friday 14.30"), qsTr("tomorrow at 9.05")]
                        delegate: RowLayout {
                            required property string modelData
                            Layout.fillWidth: true
                            spacing: 12
                            Text {
                                font.family: theme.fontFamily
                                Layout.preferredWidth: 180
                                text: "“" + parent.modelData + "”"
                                color: theme.accentText
                                font.pixelSize: Math.round(13 * theme.textScale)
                            }
                            Text {
                                font.family: theme.fontFamily
                                Layout.fillWidth: true
                                text: panel.reading(parent.modelData)
                                color: theme.foreground
                                font.pixelSize: Math.round(13 * theme.textScale)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ConfigField {
                            id: tryField
                            Layout.preferredWidth: 180
                            label: qsTr("Try it")
                            saves: false
                            placeholder: qsTr("e.g. friday 10")
                        }
                        Text {
                            font.family: theme.fontFamily
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignBottom
                            Layout.bottomMargin: 6
                            text: tryField.input.text.trim() === "" ? "" : panel.reading(tryField.input.text)
                            color: theme.foreground
                            font.pixelSize: Math.round(13 * theme.textScale)
                        }
                    }
                }

                // --- Project colours -----------------------------------------
                ColumnLayout {
                    visible: panel.section === "projects"
                    Layout.fillWidth: true
                    spacing: 4

                    Note {
                        text: qsTr("The colour shows on the project's tag and on its blocks in the calendar and timeline. The colours come from the theme's palette and follow a theme switch.")
                    }
                    Note {
                        visible: panel.projectNames.length === 0
                        Layout.topMargin: 8
                        text: qsTr("No projects yet. Give a task a project, and it shows up here.")
                    }
                    Repeater {
                        model: panel.projectNames
                        delegate: FocusScope {
                            id: projectRow
                            required property string modelData
                            // projectColor(), not a property lookup: a project may be
                            // called "constructor" or "toString".
                            readonly property string current: { void settings.projectColors; return settings.projectColor(modelData) }
                            readonly property var choices: [""].concat(settings.paletteSlots)
                            Layout.fillWidth: true
                            implicitHeight: 34
                            activeFocusOnTab: true
                            function step(delta) {
                                let i = choices.indexOf(current)
                                i = Math.max(0, Math.min(choices.length - 1, i + delta))
                                if (choices[i] !== current)
                                    panel.set("projects." + modelData, choices[i])
                            }
                            Keys.onLeftPressed: step(-1)
                            Keys.onRightPressed: step(1)
                            Keys.onDeletePressed: panel.set("projects." + modelData, "")

                            Rectangle {
                                anchors.fill: parent
                                radius: Math.min(4, theme.radius)
                                color: projectRow.activeFocus ? Qt.alpha(theme.accent, 0.12) : "transparent"
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 4
                                // The tag gives way to the swatches in a narrow panel.
                                Item {
                                    Layout.fillWidth: true
                                    Layout.maximumWidth: 180
                                    Layout.minimumWidth: 60
                                    Layout.fillHeight: true
                                    Layout.rightMargin: 8
                                    clip: true
                                    Tag {
                                        objectName: "tag:" + projectRow.modelData
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Math.min(implicitWidth, Math.floor(parent.width))
                                        text: projectRow.modelData
                                        tint: theme.palette[projectRow.current] || theme.muted
                                    }
                                }
                                Repeater {
                                    model: projectRow.choices
                                    delegate: Rectangle {
                                        id: swatch
                                        required property string modelData
                                        objectName: "swatch:" + projectRow.modelData + ":" + modelData
                                        readonly property bool chosen: projectRow.current === modelData
                                        width: 22
                                        height: 22
                                        radius: 11
                                        color: modelData === "" ? "transparent" : theme.palette[modelData] || theme.muted
                                        border.width: chosen ? 2 : 1
                                        border.color: chosen ? theme.foreground : modelData === "" ? theme.border : "transparent"
                                        // "None": a struck-through ring.
                                        Rectangle {
                                            visible: swatch.modelData === ""
                                            anchors.centerIn: parent
                                            width: parent.width - 6
                                            height: 1.5
                                            rotation: -45
                                            color: theme.muted
                                        }
                                        HoverHandler { id: swatchHover; cursorShape: Qt.PointingHandCursor }
                                        ToolTip.visible: swatchHover.hovered
                                        ToolTip.delay: 400
                                        ToolTip.text: swatch.modelData === "" ? qsTr("No colour") : swatch.modelData.replace("_", " ")
                                        TapHandler {
                                            onTapped: {
                                                projectRow.forceActiveFocus()
                                                panel.set("projects." + projectRow.modelData, swatch.modelData)
                                            }
                                        }
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                    Note {
                        visible: panel.projectNames.length > 0
                        Layout.topMargin: 6
                        text: qsTr("Tab to a project, then ← / → to pick its colour; Delete removes it.")
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.topMargin: 8; implicitHeight: 1; color: theme.border }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        font.family: theme.fontFamily
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: qsTr("Saved at once in %1").arg(settings.path)
                        color: theme.muted
                        font.pixelSize: Math.round(11 * theme.textScale)
                    }
                    HeaderButton {
                        text: qsTr("Open config.toml")
                        keyHint: "Ctrl+E"
                        keyInline: true
                        focusable: true
                        onClicked: if (!settings.openInEditor()) panel.problem = qsTr("Could not open %1").arg(settings.path)
                    }
                }
                Text {
                    font.family: theme.fontFamily
                    Layout.fillWidth: true
                    visible: panel.problem !== ""
                    text: panel.problem
                    color: theme.danger
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    // How a date text is read right now, as the task editor shows it.
    function reading(text) {
        const r = Fmt.parseDate(text)
        if (!r.ok)
            return r.error
        const d = Fmt.fromIso(r.date)
        return Fmt.capitalize(Fmt.dayLong(d)) + (r.hasTime ? " " + Fmt.time(d) : "")
    }
}
