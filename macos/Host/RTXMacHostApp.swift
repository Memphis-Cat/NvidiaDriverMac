import Foundation
import IOKit
import SwiftUI
import SystemExtensions
import UniformTypeIdentifiers

private let dextBundleIdentifier = "com.memphiscat.RTXMacHost.RTXMac"
private let driverUserClass = "RTXMacDriver"
private let maxPackageBytes = 128 * 1024 * 1024
private let rtxPackageType = UTType(filenameExtension: "rtxpkg") ?? .data

private enum UserClientSelector {
    static let validatePackage: UInt32 = 0
    static let stagePackage: UInt32 = 2
}

struct DriverValidationResult: Sendable {
    let accepted: Bool
    let parseStatus: UInt64
    let semanticFailure: UInt64
    let packageBytes: UInt64
    let liveIdentity: UInt64
    let packageIdentity: UInt64
    let driverMaxPackageBytes: UInt64

    var parseDescription: String {
        let names = [
            "ok", "too-small", "bad-magic", "unsupported-version", "bad-header",
            "bad-package-size", "bad-target", "bad-section-table",
            "duplicate-or-unknown-section", "bad-section-range", "bad-section-alignment",
            "overlapping-sections", "hash-mismatch"
        ]
        let index = Int(parseStatus)
        return index >= 0 && index < names.count ? names[index] : "unknown(\(parseStatus))"
    }

    var semanticDescription: String {
        let names = [
            "none", "package-not-verified", "missing-section", "gsp-signature-wrong-size",
            "gsp-bootloader-metadata-out-of-range", "fwsec-metadata-out-of-range",
            "sec2-metadata-out-of-range"
        ]
        let index = Int(semanticFailure)
        return index >= 0 && index < names.count ? names[index] : "unknown(\(semanticFailure))"
    }
}

struct DriverStagingResult: Sendable {
    let ready: Bool
    let stageStatus: UInt64
    let planStatus: UInt64
    let ioStatus: UInt64
    let failedSectionIndex: UInt64
    let totalLogicalBytes: UInt64
    let totalAllocationBytes: UInt64
    let totalPages: UInt64
    let firstDmaPages: [UInt64]

    var stageDescription: String {
        let names = [
            "idle", "ok", "bad-argument", "plan-rejected", "section-lookup-failed",
            "dma-allocation-failed", "dma-population-failed",
            "page-address-allocation-failed", "page-address-validation-failed"
        ]
        let index = Int(stageStatus)
        return index >= 0 && index < names.count ? names[index] : "unknown(\(stageStatus))"
    }

    var planDescription: String {
        let names = [
            "ok", "package-not-verified", "missing-or-empty-section",
            "size-overflow", "total-overflow"
        ]
        let index = Int(planStatus)
        return index >= 0 && index < names.count ? names[index] : "unknown(\(planStatus))"
    }

    var ioStatusDescription: String {
        String(format: "0x%08X", UInt32(truncatingIfNeeded: ioStatus))
    }

    var failedSectionDescription: String {
        if failedSectionIndex == UInt64(UInt32.max) { return "—" }
        let names = ["GSP firmware", "GSP signature", "GSP bootloader", "FWSEC", "SEC2 booter"]
        let index = Int(failedSectionIndex)
        return index >= 0 && index < names.count ? names[index] : "index \(failedSectionIndex)"
    }
}

private enum DriverConnectionError: LocalizedError {
    case invalidPackageSize(Int)
    case serviceNotFound
    case connectionFailed(kern_return_t)
    case methodCallFailed(UInt32, kern_return_t)
    case shortStatus(expected: UInt32, actual: UInt32)

    var errorDescription: String? {
        switch self {
        case .invalidPackageSize(let bytes):
            return "Package size \(bytes) bytes is outside the allowed 1…\(maxPackageBytes) byte range."
        case .serviceNotFound:
            return "RTXMacDriver service was not found. Activate the DriverKit extension and make sure it is attached to the RTX 3060 Ti."
        case .connectionFailed(let kr):
            return "Could not open the RTXMac user client (\(hexIOReturn(kr)))."
        case .methodCallFailed(let selector, let kr):
            return "RTXMac user-client selector \(selector) failed (\(hexIOReturn(kr)))."
        case .shortStatus(let expected, let actual):
            return "Driver returned \(actual) status values; \(expected) were required."
        }
    }
}

private func hexIOReturn(_ kr: kern_return_t) -> String {
    String(format: "0x%08X", UInt32(bitPattern: kr))
}

private func describePCIIdentity(_ packed: UInt64) -> String {
    if packed == 0 { return "—" }
    let vendor = UInt16(packed & 0xffff)
    let device = UInt16((packed >> 16) & 0xffff)
    let subsystemVendor = UInt16((packed >> 32) & 0xffff)
    let subsystemDevice = UInt16((packed >> 48) & 0xffff)
    return String(
        format: "%04X:%04X subsystem %04X:%04X",
        vendor, device, subsystemVendor, subsystemDevice
    )
}

private func describeDmaAddress(_ address: UInt64) -> String {
    address == 0 ? "—" : String(format: "0x%016llX", address)
}

private func findRTXMacService() -> io_service_t {
    let named = IOServiceGetMatchingService(
        kIOMainPortDefault,
        IOServiceNameMatching(driverUserClass)
    )
    if named != IO_OBJECT_NULL { return named }

    let byClass = IOServiceGetMatchingService(
        kIOMainPortDefault,
        IOServiceMatching(driverUserClass)
    )
    if byClass != IO_OBJECT_NULL { return byClass }

    var iterator: io_iterator_t = IO_OBJECT_NULL
    guard IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("IOUserService"),
        &iterator
    ) == KERN_SUCCESS else {
        return IO_OBJECT_NULL
    }
    defer { IOObjectRelease(iterator) }

    while true {
        let service = IOIteratorNext(iterator)
        if service == IO_OBJECT_NULL { return IO_OBJECT_NULL }

        if let property = IORegistryEntryCreateCFProperty(
            service,
            "IOUserClass" as CFString,
            kCFAllocatorDefault,
            0
        )?.takeRetainedValue() as? String,
           property == driverUserClass {
            return service
        }
        IOObjectRelease(service)
    }
}

private final class RTXMacDriverSession: @unchecked Sendable {
    let connection: io_connect_t

    private init(connection: io_connect_t) {
        self.connection = connection
    }

    deinit {
        if connection != IO_OBJECT_NULL {
            IOServiceClose(connection)
        }
    }

    static func open() throws -> RTXMacDriverSession {
        let service = findRTXMacService()
        guard service != IO_OBJECT_NULL else {
            throw DriverConnectionError.serviceNotFound
        }
        defer { IOObjectRelease(service) }

        var connection: io_connect_t = IO_OBJECT_NULL
        let openResult = IOServiceOpen(service, mach_task_self_, 0, &connection)
        guard openResult == KERN_SUCCESS else {
            throw DriverConnectionError.connectionFailed(openResult)
        }
        return RTXMacDriverSession(connection: connection)
    }

    func callPackageMethod(
        _ data: Data,
        selector: UInt32,
        expectedOutputCount: UInt32
    ) throws -> [UInt64] {
        guard !data.isEmpty && data.count <= maxPackageBytes else {
            throw DriverConnectionError.invalidPackageSize(data.count)
        }

        var output = [UInt64](repeating: 0, count: Int(expectedOutputCount))
        var outputCount = expectedOutputCount
        let callResult: kern_return_t = data.withUnsafeBytes { input in
            output.withUnsafeMutableBufferPointer { outputBuffer in
                IOConnectCallMethod(
                    connection,
                    selector,
                    nil,
                    0,
                    input.baseAddress,
                    input.count,
                    outputBuffer.baseAddress,
                    &outputCount,
                    nil,
                    nil
                )
            }
        }
        guard callResult == KERN_SUCCESS else {
            throw DriverConnectionError.methodCallFailed(selector, callResult)
        }
        guard outputCount == expectedOutputCount else {
            throw DriverConnectionError.shortStatus(
                expected: expectedOutputCount,
                actual: outputCount
            )
        }
        return output
    }
}

private func validatePackageWithDriver(_ data: Data) throws -> DriverValidationResult {
    let session = try RTXMacDriverSession.open()
    let output = try session.callPackageMethod(
        data,
        selector: UserClientSelector.validatePackage,
        expectedOutputCount: 8
    )

    return DriverValidationResult(
        accepted: output[0] != 0 && output[1] != 0,
        parseStatus: output[2],
        semanticFailure: output[3],
        packageBytes: output[4],
        liveIdentity: output[5],
        packageIdentity: output[6],
        driverMaxPackageBytes: output[7]
    )
}

private func stagePackageWithDriver(
    _ data: Data
) throws -> (DriverStagingResult, RTXMacDriverSession) {
    let session = try RTXMacDriverSession.open()
    let output = try session.callPackageMethod(
        data,
        selector: UserClientSelector.stagePackage,
        expectedOutputCount: 13
    )

    let result = DriverStagingResult(
        ready: output[0] != 0,
        stageStatus: output[1],
        planStatus: output[2],
        ioStatus: output[3],
        failedSectionIndex: output[4],
        totalLogicalBytes: output[5],
        totalAllocationBytes: output[6],
        totalPages: output[7],
        firstDmaPages: Array(output[8...12])
    )
    return (result, session)
}

@MainActor
final class ExtensionManager: NSObject, ObservableObject, OSSystemExtensionRequestDelegate {
    @Published var status = "Driver not activated by this app yet."
    @Published var packageStatus = "No package selected."
    @Published var packageName = "—"
    @Published var validation: DriverValidationResult?
    @Published var validating = false
    @Published var canStage = false
    @Published var staging = false
    @Published var stagingStatus = "Package is not staged."
    @Published var stagingResult: DriverStagingResult?

    private var selectedPackageData: Data?
    private var stagedSession: RTXMacDriverSession?

    func activate() {
        status = "Submitting DriverKit activation request…"
        let request = OSSystemExtensionRequest.activationRequest(
            forExtensionWithIdentifier: dextBundleIdentifier,
            queue: .main
        )
        request.delegate = self
        OSSystemExtensionManager.shared.submitRequest(request)
    }

    func validatePackage(at url: URL) {
        packageName = url.lastPathComponent
        packageStatus = "Reading and validating \(url.lastPathComponent)…"
        validation = nil
        validating = true
        canStage = false
        selectedPackageData = nil
        stagedSession = nil
        stagingResult = nil
        stagingStatus = "Package is not staged."

        Task.detached(priority: .userInitiated) {
            do {
                let securityScoped = url.startAccessingSecurityScopedResource()
                defer {
                    if securityScoped { url.stopAccessingSecurityScopedResource() }
                }

                if let fileSize = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize,
                   (fileSize <= 0 || fileSize > maxPackageBytes) {
                    throw DriverConnectionError.invalidPackageSize(fileSize)
                }

                let data = try Data(contentsOf: url, options: [.mappedIfSafe])
                let result = try validatePackageWithDriver(data)
                await MainActor.run {
                    self.validation = result
                    self.validating = false
                    self.canStage = result.accepted
                    self.selectedPackageData = result.accepted ? data : nil
                    self.packageStatus = result.accepted
                        ? "Package accepted by the DriverKit validator."
                        : "Package rejected. No GPU write or firmware execution occurred."
                }
            } catch {
                await MainActor.run {
                    self.validating = false
                    self.validation = nil
                    self.canStage = false
                    self.selectedPackageData = nil
                    self.packageStatus = error.localizedDescription
                }
            }
        }
    }

    func stageSelectedPackage() {
        guard let data = selectedPackageData, validation?.accepted == true else {
            stagingStatus = "Validate an accepted .rtxpkg before staging."
            return
        }

        staging = true
        stagingResult = nil
        stagingStatus = "Allocating, zero-padding, populating, and preparing SYSRAM DMA buffers…"
        stagedSession = nil

        Task.detached(priority: .userInitiated) {
            do {
                let (result, session) = try stagePackageWithDriver(data)
                await MainActor.run {
                    self.staging = false
                    self.stagingResult = result
                    if result.ready {
                        // Retaining the connection retains the DriverKit user client and
                        // therefore all prepared package DMA state for later cold stages.
                        self.stagedSession = session
                        self.stagingStatus = "Package staged in prepared SYSRAM. The DriverKit connection is being kept open."
                    } else {
                        self.stagedSession = nil
                        self.stagingStatus = "Package staging failed: \(result.stageDescription), I/O \(result.ioStatusDescription)."
                    }
                }
            } catch {
                await MainActor.run {
                    self.staging = false
                    self.stagingResult = nil
                    self.stagedSession = nil
                    self.stagingStatus = error.localizedDescription
                }
            }
        }
    }

    func packageImportFailed(_ error: Error) {
        packageStatus = "Package selection failed: \(error.localizedDescription)"
        validation = nil
        canStage = false
        selectedPackageData = nil
        stagedSession = nil
        stagingResult = nil
        stagingStatus = "Package is not staged."
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

private struct RTXMacContentView: View {
    @ObservedObject var extensions: ExtensionManager
    @State private var importingPackage = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("RTXMac")
                    .font(.largeTitle.bold())
                Text("Cold Ampere DriverKit research probe")
                    .foregroundStyle(.secondary)

                GroupBox("DriverKit extension") {
                    VStack(alignment: .leading, spacing: 10) {
                        Text(extensions.status)
                            .textSelection(.enabled)
                        Button("Activate read-only driver") {
                            extensions.activate()
                        }
                        .buttonStyle(.borderedProminent)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                GroupBox("Validated boot package") {
                    VStack(alignment: .leading, spacing: 10) {
                        HStack {
                            Button(extensions.validating ? "Validating…" : "Select .rtxpkg and validate") {
                                importingPackage = true
                            }
                            .disabled(extensions.validating || extensions.staging)

                            Text(extensions.packageName)
                                .foregroundStyle(.secondary)
                                .lineLimit(1)
                        }

                        Text(extensions.packageStatus)
                            .textSelection(.enabled)

                        if let result = extensions.validation {
                            Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 6) {
                                GridRow {
                                    Text("Accepted")
                                    Text(result.accepted ? "YES" : "NO")
                                }
                                GridRow {
                                    Text("Container / hashes")
                                    Text(result.parseDescription)
                                }
                                GridRow {
                                    Text("GA10x semantics")
                                    Text(result.semanticDescription)
                                }
                                GridRow {
                                    Text("Package size")
                                    Text("\(result.packageBytes) bytes")
                                }
                                GridRow {
                                    Text("Package PCI")
                                    Text(describePCIIdentity(result.packageIdentity))
                                }
                                GridRow {
                                    Text("Attached PCI")
                                    Text(describePCIIdentity(result.liveIdentity))
                                }
                                GridRow {
                                    Text("Driver input limit")
                                    Text("\(result.driverMaxPackageBytes) bytes")
                                }
                            }
                            .font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                GroupBox("Cold SYSRAM staging") {
                    VStack(alignment: .leading, spacing: 10) {
                        Button(extensions.staging ? "Staging…" : "Stage verified package in SYSRAM") {
                            extensions.stageSelectedPackage()
                        }
                        .disabled(!extensions.canStage || extensions.staging || extensions.validating)

                        Text(extensions.stagingStatus)
                            .textSelection(.enabled)

                        if let result = extensions.stagingResult {
                            Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 6) {
                                GridRow { Text("Ready"); Text(result.ready ? "YES" : "NO") }
                                GridRow { Text("Stage status"); Text(result.stageDescription) }
                                GridRow { Text("DMA plan"); Text(result.planDescription) }
                                GridRow { Text("Driver I/O"); Text(result.ioStatusDescription) }
                                GridRow { Text("Failed section"); Text(result.failedSectionDescription) }
                                GridRow { Text("Logical bytes"); Text("\(result.totalLogicalBytes)") }
                                GridRow { Text("Allocated bytes"); Text("\(result.totalAllocationBytes)") }
                                GridRow { Text("DMA pages"); Text("\(result.totalPages)") }
                                GridRow { Text("GSP firmware first page"); Text(describeDmaAddress(result.firstDmaPages[0])) }
                                GridRow { Text("GSP signature first page"); Text(describeDmaAddress(result.firstDmaPages[1])) }
                                GridRow { Text("GSP bootloader first page"); Text(describeDmaAddress(result.firstDmaPages[2])) }
                                GridRow { Text("FWSEC first page"); Text(describeDmaAddress(result.firstDmaPages[3])) }
                                GridRow { Text("SEC2 booter first page"); Text(describeDmaAddress(result.firstDmaPages[4])) }
                            }
                            .font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                Text("Cold staging allocates and prepares host SYSRAM only. It does not reset the GPU, write BAR0/PRAMIN, change clocks or power, execute Falcon firmware, or start GSP-RM.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(24)
        }
        .frame(minWidth: 780, minHeight: 620)
        .fileImporter(
            isPresented: $importingPackage,
            allowedContentTypes: [rtxPackageType],
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                if let url = urls.first {
                    extensions.validatePackage(at: url)
                }
            case .failure(let error):
                extensions.packageImportFailed(error)
            }
        }
    }
}

@main
struct RTXMacHostApp: App {
    @StateObject private var extensions = ExtensionManager()

    var body: some Scene {
        WindowGroup {
            RTXMacContentView(extensions: extensions)
        }
    }
}
