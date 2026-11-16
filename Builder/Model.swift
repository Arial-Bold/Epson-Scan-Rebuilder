import AppKit
import Foundation

/// Where the builder is, step by step (the screens of the mockup).
enum Stage: Equatable {
    case welcome
    case needTools(URL)          // the installer is chosen, Apple's tools are missing
    case checking(URL)
    case ready(URL)              // installer recognized, tools installed
    case wrong(URL, String)      // not the installer this builder knows
    case building
    case done(URL)               // the built app
    case failed(String)
}

final class BuilderModel: ObservableObject {
    static let downloadPage = URL(string: "https://epson.com/Support/Scanners/Perfection-Series/Epson-Perfection-V550-Photo/s/SPT_B11B210201")!
    static let steps = ["Opening the installer", "Reading Epson's files", "Converting Epson's code for Apple Silicon",
                        "Copying the windows, artwork and help", "Putting the app together"]

    @Published var stage: Stage = .welcome
    @Published var step = 0                  // 1...5 while building
    @Published var parts = (done: 0, total: 12)
    @Published var detail = ""               // the line under the title
    @Published var progress = 0.0            // 0...1
    @Published var log: [String] = []

    private var process: Process?
    private var toolsTimer: Timer?
    private var compileLines = 0

    // MARK: Apple's Command Line Tools (clang, python3)

    /// The developer folder `xcode-select -p` reports, if it has a compiler and Python.
    var developerDir: String? {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/xcode-select")
        p.arguments = ["-p"]
        let pipe = Pipe()
        p.standardOutput = pipe
        p.standardError = Pipe()
        do { try p.run() } catch { return nil }
        p.waitUntilExit()
        guard p.terminationStatus == 0,
              let dir = String(data: pipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8)?
                .trimmingCharacters(in: .whitespacesAndNewlines), !dir.isEmpty else { return nil }
        let fm = FileManager.default
        guard fm.isExecutableFile(atPath: dir + "/usr/bin/clang"),
              fm.isExecutableFile(atPath: dir + "/usr/bin/python3") else { return nil }
        return dir
    }

    var toolsInstalled: Bool { developerDir != nil }

    func installTools() {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/xcode-select")
        p.arguments = ["--install"]
        try? p.run()
        watchForTools()
    }

    /// The builder carries on by itself once the tools are there.
    func watchForTools() {
        toolsTimer?.invalidate()
        toolsTimer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] t in
            guard let self = self else { t.invalidate(); return }
            if case .needTools(let url) = self.stage, self.toolsInstalled {
                t.invalidate()
                self.check(url)
            }
        }
    }

    // MARK: Choosing the installer

    func choose(_ url: URL) {
        guard toolsInstalled else {
            stage = .needTools(url)
            watchForTools()
            return
        }
        check(url)
    }

    func chooseFile() {
        let panel = NSOpenPanel()
        panel.title = "Choose the Epson installer"
        panel.allowedFileTypes = ["dmg", "pkg"]
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url { choose(url) }
    }

    func check(_ url: URL) {
        stage = .checking(url)
        run(["check", url.path]) { [weak self] line in
            guard let self = self, line.hasPrefix("CHECK ") else { return }
            let answer = String(line.dropFirst(6))
            if answer == "ok" { self.stage = .ready(url) }
            else { self.stage = .wrong(url, answer.hasPrefix("wrong ") ? String(answer.dropFirst(6)) : answer) }
        } finished: { [weak self] status in
            guard let self = self else { return }
            if case .checking = self.stage { self.stage = .wrong(url, "the builder couldn't read this file") }
        }
    }

    // MARK: Building

    var outputFolder: URL {
        FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory())
    }

    func build(_ url: URL) {
        stage = .building
        step = 1; parts = (0, 12); progress = 0; log = []; compileLines = 0
        detail = Self.steps[0] + "…"
        run(["build", url.path, outputFolder.path]) { [weak self] line in
            self?.handle(line, installer: url)
        } finished: { [weak self] status in
            guard let self = self else { return }
            if case .building = self.stage {
                self.stage = .failed(status == 15 ? "The build was cancelled." : "The builder stopped unexpectedly.")
            }
        }
    }

    func cancel() {
        process?.terminate()
    }

    private func handle(_ line: String, installer: URL) {
        let fields = line.split(separator: " ", maxSplits: 3).map(String.init)
        guard let kind = fields.first else { return }
        switch kind {
        case "STEP":
            if fields.count > 1, let n = Int(fields[1]) {
                step = n
                detail = Self.steps[min(n, Self.steps.count) - 1] + "…"
                progress = [0, 0.0, 0.04, 0.08, 0.30, 0.34][min(n, 5)]
            }
        case "PART":
            if fields.count > 3, let i = Int(fields[1]), let t = Int(fields[2]) {
                parts = (i, t)
                detail = fields[3].replacingOccurrences(of: "Converted ", with: "Converting ") + "…"
                progress = 0.08 + 0.22 * Double(i) / Double(max(t, 1))
            }
        case "LOG":
            let text = fields.count > 1 ? String(line.dropFirst(4)) : ""
            log.append(text)
            if step == 5 {
                // build_app.sh announces the big engines; the rest goes quickly
                if text.hasPrefix("Compiling") {
                    compileLines += 1
                    detail = text
                }
                progress = min(0.98, 0.34 + 0.09 * Double(compileLines))
            }
        case "DONE":
            progress = 1
            stage = .done(URL(fileURLWithPath: String(line.dropFirst(5))))
        case "FAIL":
            let msg = String(line.dropFirst(5))
            if msg.hasPrefix("That isn't the right installer") {
                stage = .wrong(installer, String(msg.drop(while: { $0 != ":" }).dropFirst(2)))
            } else {
                stage = .failed(msg)
            }
        default:
            log.append(line)
        }
    }

    // MARK: The engine (Python, with Apple's tools)

    private func run(_ args: [String], line: @escaping (String) -> Void, finished: @escaping (Int32) -> Void) {
        guard let dev = developerDir,
              let engine = Bundle.main.url(forResource: "engine", withExtension: nil) else {
            stage = .failed("The builder is missing its engine.")
            return
        }
        let p = Process()
        p.executableURL = URL(fileURLWithPath: dev + "/usr/bin/python3")
        p.arguments = ["-u", engine.appendingPathComponent("build.py").path] + args
        var env = ProcessInfo.processInfo.environment
        env["PATH"] = dev + "/usr/bin:/usr/bin:/bin:/usr/sbin:/sbin"
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        p.environment = env
        let pipe = Pipe()
        p.standardOutput = pipe
        p.standardError = pipe
        var buffer = Data()
        pipe.fileHandleForReading.readabilityHandler = { h in
            let chunk = h.availableData
            guard !chunk.isEmpty else { return }
            buffer.append(chunk)
            while let nl = buffer.firstIndex(of: 0x0A) {
                let lineData = buffer[buffer.startIndex..<nl]
                buffer.removeSubrange(buffer.startIndex...nl)
                if let s = String(data: lineData, encoding: .utf8) {
                    DispatchQueue.main.async { line(s) }
                }
            }
        }
        p.terminationHandler = { proc in
            pipe.fileHandleForReading.readabilityHandler = nil
            let rest = pipe.fileHandleForReading.readDataToEndOfFile()
            DispatchQueue.main.async {
                if let s = String(data: buffer + rest, encoding: .utf8) {
                    s.split(separator: "\n").forEach { line(String($0)) }
                }
                finished(proc.terminationStatus)
            }
        }
        do {
            try p.run()
            process = p
        } catch {
            stage = .failed("Couldn't start Python: \(error.localizedDescription)")
        }
    }

    // MARK: The finished app

    func showInFinder(_ app: URL) { NSWorkspace.shared.activateFileViewerSelecting([app]) }

    func open(_ app: URL) { NSWorkspace.shared.open(app) }

    func moveToApplications(_ app: URL) {
        let dest = URL(fileURLWithPath: "/Applications").appendingPathComponent(app.lastPathComponent)
        let fm = FileManager.default
        do {
            if fm.fileExists(atPath: dest.path) {
                let a = NSAlert()
                a.messageText = "Replace the EPSON Scan app in Applications?"
                a.informativeText = "There is already an app with this name in the Applications folder."
                a.addButton(withTitle: "Replace")
                a.addButton(withTitle: "Cancel")
                guard a.runModal() == .alertFirstButtonReturn else { return }
                try fm.trashItem(at: dest, resultingItemURL: nil)
            }
            try fm.moveItem(at: app, to: dest)
            stage = .done(dest)
            NSWorkspace.shared.activateFileViewerSelecting([dest])
        } catch {
            let a = NSAlert(error: error)
            a.runModal()
        }
    }
}
