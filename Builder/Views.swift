import AppKit
import SwiftUI

let accent = Color(red: 10 / 255, green: 100 / 255, blue: 216 / 255)          // #0A64D8
let secondaryText = Color(red: 110 / 255, green: 110 / 255, blue: 115 / 255)   // #6E6E73
let bodyText = Color(red: 58 / 255, green: 58 / 255, blue: 60 / 255)           // #3A3A3C
let titleText = Color(red: 29 / 255, green: 29 / 255, blue: 31 / 255)          // #1D1D1F

struct RootView: View {
    @ObservedObject var model: BuilderModel

    var body: some View {
        Group {
            switch model.stage {
            case .welcome: WelcomeView(model: model)
            case .needTools: ToolsView(model: model)
            case .checking: CheckingView()
            case .ready(let url): ReadyView(model: model, installer: url)
            case .wrong(_, let reason): WrongView(model: model, reason: reason)
            case .building: BuildingView(model: model)
            case .done(let app): DoneView(model: model, app: app)
            case .failed(let message): FailedView(model: model, message: message)
            }
        }
        .padding(EdgeInsets(top: 28, leading: 40, bottom: 24, trailing: 40))
        .frame(width: 720, height: 468)
        .background(Color.white)
        .foregroundColor(titleText)
        .tint(accent)
    }
}

/// the big rounded icon and the title of each screen
struct Header: View {
    var symbol: String
    var tint: Color = accent
    var title: String
    var subtitle: String

    var body: some View {
        HStack(spacing: 16) {
            ZStack {
                RoundedRectangle(cornerRadius: 14).fill(tint)
                Image(systemName: symbol).font(.system(size: 26, weight: .medium)).foregroundColor(.white)
            }
            .frame(width: 56, height: 56)
            VStack(alignment: .leading, spacing: 4) {
                Text(title).font(.system(size: 24, weight: .bold))
                Text(subtitle).font(.system(size: 13)).foregroundColor(secondaryText)
            }
            Spacer()
        }
    }
}

struct PrimaryButton: View {
    var title: String
    var enabled = true
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title).font(.system(size: 13, weight: .semibold))
                .padding(.horizontal, 20).frame(height: 32)
                .foregroundColor(enabled ? .white : Color(white: 0.56))
                .background(RoundedRectangle(cornerRadius: 8).fill(enabled ? accent : Color(white: 0.9)))
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .keyboardShortcut(.defaultAction)
    }
}

struct SecondaryButton: View {
    var title: String
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title).font(.system(size: 13))
                .padding(.horizontal, 14).frame(height: 32)
                .background(RoundedRectangle(cornerRadius: 8).stroke(Color(white: 0.82)))
        }
        .buttonStyle(.plain)
    }
}

struct NumberedStep: View {
    var n: Int
    var text: String

    var body: some View {
        HStack(spacing: 12) {
            Text("\(n)").font(.system(size: 12, weight: .bold)).foregroundColor(Color(red: 10 / 255, green: 95 / 255, blue: 208 / 255))
                .frame(width: 22, height: 22).background(Circle().fill(Color(red: 232 / 255, green: 240 / 255, blue: 252 / 255)))
            Text(text).font(.system(size: 13))
        }
    }
}

// MARK: Welcome

struct WelcomeView: View {
    @ObservedObject var model: BuilderModel

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Header(symbol: "scanner", title: "Build the scanner app", subtitle: "For the Epson Perfection V550 on Apple Silicon Macs")
            Text("This makes a native version of Epson Scan 3.9.4 from Epson's own installer. Nothing from Epson comes with this builder — it all comes from the installer you download.")
                .font(.system(size: 13)).foregroundColor(bodyText).fixedSize(horizontal: false, vertical: true)
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    NumberedStep(n: 1, text: "Download Epson Scan 3.9.4 for the Perfection V550.")
                    Spacer()
                    SecondaryButton(title: "Open Epson's download page ↗") { NSWorkspace.shared.open(BuilderModel.downloadPage) }
                }
                NumberedStep(n: 2, text: "Drop the installer below.")
                NumberedStep(n: 3, text: "Click Build. It takes a few minutes.")
            }
            DropZone(model: model)
            HStack { Spacer(); PrimaryButton(title: "Build", enabled: false) {} }
        }
    }
}

struct DropZone: View {
    @ObservedObject var model: BuilderModel
    @State private var targeted = false

    var body: some View {
        VStack(spacing: 8) {
            Image(systemName: "square.and.arrow.down").font(.system(size: 30)).foregroundColor(Color(white: 0.56))
            Text("Drop the Epson installer here").font(.system(size: 15, weight: .semibold))
            Button("or Choose File…") { model.chooseFile() }.buttonStyle(.plain).foregroundColor(accent).font(.system(size: 13))
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(RoundedRectangle(cornerRadius: 14).fill(targeted ? Color(red: 232 / 255, green: 240 / 255, blue: 252 / 255)
                                                                     : Color(red: 250 / 255, green: 250 / 255, blue: 252 / 255)))
        .overlay(RoundedRectangle(cornerRadius: 14).strokeBorder(targeted ? accent : Color(white: 0.78),
                                                                 style: StrokeStyle(lineWidth: 2, dash: [6, 4])))
        .onDrop(of: ["public.file-url"], isTargeted: $targeted) { providers in
            guard let p = providers.first else { return false }
            p.loadItem(forTypeIdentifier: "public.file-url", options: nil) { item, _ in
                var url: URL?
                if let data = item as? Data { url = URL(dataRepresentation: data, relativeTo: nil) }
                else if let u = item as? URL { url = u }
                if let url = url { DispatchQueue.main.async { model.choose(url) } }
            }
            return true
        }
    }
}

// MARK: Tools

struct ToolsView: View {
    @ObservedObject var model: BuilderModel

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Header(symbol: "hammer", title: "One more thing from Apple", subtitle: "Apple's free Command Line Tools")
            Text("The builder uses Apple's free Command Line Tools to put the app together. Click Install and follow Apple's window. You only need to do this once.")
                .font(.system(size: 13)).foregroundColor(bodyText).fixedSize(horizontal: false, vertical: true)
            Spacer()
            Text("The builder carries on by itself once the tools are installed.").font(.system(size: 12)).foregroundColor(secondaryText)
            HStack {
                Spacer()
                SecondaryButton(title: "Check Again") {
                    if case .needTools(let url) = model.stage { model.choose(url) }
                }
                PrimaryButton(title: "Install Tools…") { model.installTools() }
            }
        }
    }
}

// MARK: Checking / ready / wrong installer

struct CheckingView: View {
    var body: some View {
        VStack(spacing: 14) {
            Spacer()
            ProgressView()
            Text("Looking at the installer…").font(.system(size: 13)).foregroundColor(secondaryText)
            Spacer()
        }
        .frame(maxWidth: .infinity)
    }
}

struct CheckRow: View {
    var title: String
    var subtitle: String
    var status: String

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "checkmark.circle.fill").font(.system(size: 22)).foregroundColor(Color(red: 40 / 255, green: 160 / 255, blue: 70 / 255))
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.system(size: 13, weight: .semibold))
                Text(subtitle).font(.system(size: 12)).foregroundColor(secondaryText)
            }
            Spacer()
            Text(status).font(.system(size: 12)).foregroundColor(secondaryText)
        }
        .padding(14)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color(red: 245 / 255, green: 245 / 255, blue: 247 / 255)))
    }
}

struct ReadyView: View {
    @ObservedObject var model: BuilderModel
    var installer: URL

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Header(symbol: "checkmark", title: "Ready to build", subtitle: "Everything the builder needs is here.")
            CheckRow(title: "Epson Scan 3.9.4 installer", subtitle: "For the Perfection V550 · the version this builder knows", status: "Recognized")
            CheckRow(title: "Apple Command Line Tools", subtitle: "Used to compile the app on this Mac", status: "Installed")
            Spacer()
            Text("Building takes a few minutes. You can keep using your Mac while it works.").font(.system(size: 12)).foregroundColor(secondaryText)
            HStack {
                Spacer()
                SecondaryButton(title: "Choose Another File…") { model.chooseFile() }
                PrimaryButton(title: "Build") { model.build(installer) }
            }
        }
    }
}

struct WrongView: View {
    @ObservedObject var model: BuilderModel
    var reason: String

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Header(symbol: "exclamationmark", tint: Color(red: 1, green: 0.62, blue: 0.04),
                   title: "That isn't the right installer", subtitle: "The builder only works with one exact version.")
            Text("This file is a different version of Epson Scan, or a different scanner's installer. Download Epson Scan 3.9.4 for the Perfection V550 from Epson's support site, then drop that file here.")
                .font(.system(size: 13)).foregroundColor(bodyText).fixedSize(horizontal: false, vertical: true)
            Text("Details: " + reason).font(.system(size: 12)).foregroundColor(secondaryText)
            Spacer()
            HStack {
                SecondaryButton(title: "Open Epson's download page ↗") { NSWorkspace.shared.open(BuilderModel.downloadPage) }
                Spacer()
                PrimaryButton(title: "Choose Another File…") { model.chooseFile() }
            }
        }
    }
}

// MARK: Building

struct BuildingView: View {
    @ObservedObject var model: BuilderModel
    @State private var showDetails = false

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Header(symbol: "gearshape.2", title: "Building the scanner app…",
                   subtitle: "This takes a few minutes. You can keep using your Mac.")
            ProgressView(value: model.progress).progressViewStyle(.linear).tint(accent)
            Text(model.detail).font(.system(size: 12)).foregroundColor(secondaryText)
            if showDetails {
                ScrollView {
                    Text(model.log.suffix(400).joined(separator: "\n"))
                        .font(.system(size: 11, design: .monospaced)).frame(maxWidth: .infinity, alignment: .leading)
                        .textSelection(.enabled)
                }
                .padding(8)
                .background(RoundedRectangle(cornerRadius: 8).fill(Color(red: 245 / 255, green: 245 / 255, blue: 247 / 255)))
            } else {
                VStack(alignment: .leading, spacing: 9) {
                    ForEach(0..<BuilderModel.steps.count, id: \.self) { i in
                        StepRow(title: BuilderModel.steps[i], state: i + 1 < model.step ? 2 : i + 1 == model.step ? 1 : 0,
                                trailing: i == 2 && model.step == 3 ? "\(model.parts.done) of \(model.parts.total)" : "")
                    }
                }
                Spacer()
            }
            HStack {
                Button(showDetails ? "Hide details" : "Show details") { showDetails.toggle() }
                    .buttonStyle(.plain).foregroundColor(accent).font(.system(size: 13))
                Spacer()
                SecondaryButton(title: "Cancel") { model.cancel() }
            }
        }
    }
}

struct StepRow: View {
    var title: String
    var state: Int          // 0 waiting, 1 working, 2 done
    var trailing: String

    var body: some View {
        HStack(spacing: 10) {
            Group {
                if state == 2 {
                    Image(systemName: "checkmark.circle.fill").foregroundColor(Color(red: 40 / 255, green: 160 / 255, blue: 70 / 255))
                } else if state == 1 {
                    ProgressView().scaleEffect(0.5)
                } else {
                    Image(systemName: "circle").foregroundColor(Color(white: 0.78))
                }
            }
            .frame(width: 18, height: 18)
            Text(title).font(.system(size: 13, weight: state == 1 ? .semibold : .regular))
                .foregroundColor(state == 0 ? secondaryText : titleText)
            Spacer()
            Text(trailing).font(.system(size: 12)).foregroundColor(secondaryText)
        }
    }
}

// MARK: Done / failed

struct DoneView: View {
    @ObservedObject var model: BuilderModel
    var app: URL

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Header(symbol: "checkmark", tint: Color(red: 40 / 255, green: 160 / 255, blue: 70 / 255),
                   title: "EPSON Scan is ready", subtitle: "Built on this Mac for Apple Silicon.")
            Text(app.deletingLastPathComponent().path == "/Applications"
                 ? "It's in your Applications folder. You can delete the Epson installer now."
                 : "It's in your Downloads folder. Move it to Applications to keep it. You can delete the Epson installer now.")
                .font(.system(size: 13)).foregroundColor(bodyText).fixedSize(horizontal: false, vertical: true)
            Text("If macOS says the app can't be opened the first time, Control-click it, choose Open, then click Open again.")
                .font(.system(size: 12)).foregroundColor(secondaryText).fixedSize(horizontal: false, vertical: true)
            Spacer()
            HStack {
                SecondaryButton(title: "Show in Finder") { model.showInFinder(app) }
                Spacer()
                SecondaryButton(title: "Open") { model.open(app) }
                if app.deletingLastPathComponent().path != "/Applications" {
                    PrimaryButton(title: "Move to Applications") { model.moveToApplications(app) }
                }
            }
        }
    }
}

struct FailedView: View {
    @ObservedObject var model: BuilderModel
    var message: String

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Header(symbol: "xmark", tint: Color(red: 1, green: 0.27, blue: 0.23),
                   title: "The app couldn't be built", subtitle: "Nothing was changed on your Mac.")
            ScrollView {
                Text(message).font(.system(size: 12, design: .monospaced)).frame(maxWidth: .infinity, alignment: .leading)
                    .textSelection(.enabled)
            }
            .padding(8)
            .background(RoundedRectangle(cornerRadius: 8).fill(Color(red: 245 / 255, green: 245 / 255, blue: 247 / 255)))
            HStack {
                SecondaryButton(title: "Copy Details") {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(message + "\n\n" + model.log.suffix(200).joined(separator: "\n"), forType: .string)
                }
                Spacer()
                PrimaryButton(title: "Start Over") { model.stage = .welcome }
            }
        }
    }
}
