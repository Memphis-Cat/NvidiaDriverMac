import SwiftUI
import SystemExtensions

private let dextBundleIdentifier = "com.memphiscat.RTXMacHost.RTXMac"

@MainActor
final class ExtensionManager: NSObject, ObservableObject, OSSystemExtensionRequestDelegate {
    @Published var status = "Driver not activated by this app yet."

    func activate() {
        status = "Submitting DriverKit activation request…"
        let request = OSSystemExtensionRequest.activationRequest(
            forExtensionWithIdentifier: dextBundleIdentifier,
            queue: .main
        )
        request.delegate = self
        OSSystemExtensionManager.shared.submitRequest(request)
    }

    nonisolated func request(
        _ request: OSSystemExtensionRequest,
        actionForReplacingExtension existing: OSSystemExtensionProperties,
        withExtension ext: OSSystemExtensionProperties
    ) -> OSSystemExtensionRequest.ReplacementAction {
        .replace
    }

    nonisolated func requestNeedsUserApproval(_ request: OSSystemExtensionRequest) {
        Task { @MainActor in
            self.status = "macOS needs approval in System Settings → General → Login Items & Extensions → Driver Extensions."
        }
    }

    nonisolated func request(
        _ request: OSSystemExtensionRequest,
        didFinishWithResult result: OSSystemExtensionRequest.Result
    ) {
        Task { @MainActor in
            self.status = "Driver activation request finished (result: \(result.rawValue))."
        }
    }

    nonisolated func request(_ request: OSSystemExtensionRequest, didFailWithError error: Error) {
        Task { @MainActor in
            self.status = "Driver activation failed: \(error.localizedDescription)"
        }
    }
}

@main
struct RTXMacHostApp: App {
    @StateObject private var extensions = ExtensionManager()

    var body: some Scene {
        WindowGroup {
            VStack(alignment: .leading, spacing: 16) {
                Text("RTXMac")
                    .font(.largeTitle.bold())
                Text("Read-only Ampere DriverKit research probe")
                    .foregroundStyle(.secondary)

                Text(extensions.status)
                    .textSelection(.enabled)

                Button("Activate read-only driver") {
                    extensions.activate()
                }
                .buttonStyle(.borderedProminent)

                Divider()

                Text("This prototype does not flash firmware, reset the GPU, alter clocks/power, or expose register writes.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(24)
            .frame(minWidth: 620, minHeight: 260)
        }
    }
}
