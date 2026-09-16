import Foundation
import IOKit
import SwiftUI
import SystemExtensions
import UniformTypeIdentifiers

private let dextBundleIdentifier = "com.memphiscat.RTXMacHost.RTXMac"
private let driverUserClass = "RTXMacDriver"
private let maxPackageBytes = 128 * 1024 * 1024
private let rtxPackageType = UTType(filenameExtension: "rtxpkg") ?? .data

struct RTXMacDiagnosticDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.json] }

    var data: Data

    init(data: Data = Data()) {
        self.data = data
    }

    init(configuration: ReadConfiguration) throws {
        data = configuration.file.regularFileContents ?? Data()
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: data)
    }
}

private enum UserClientSelector {
    static let validatePackage: UInt32 = 0
    static let stagePackage: UInt32 = 2
    static let systemInfo: UInt32 = 4
    static let liveBoundary: UInt32 = 5
    static let prepareColdBootSession: UInt32 = 6
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
            "page-address-allocation-failed", "page-address-validation-failed",
            "dma-layout-rejected", "dma-resolve-failed"
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

struct DriverSystemInfoResult: Sendable {
    let available: Bool
    let ioStatus: UInt64
    let bdf: UInt64
    let deviceVendor: UInt64
    let subsystem: UInt64
    let revision: UInt64
    let bar0Base: UInt64
    let bar0Size: UInt64
    let bar1Base: UInt64
    let bar1Size: UInt64
    let bar3Base: UInt64
    let bar3Size: UInt64
    let maxUserVa: UInt64
    let pciConfigMirrorBase: UInt64
    let pciConfigMirrorSize: UInt64
    let passthrough: Bool

    var ioStatusDescription: String {
        String(format: "0x%08X", UInt32(truncatingIfNeeded: ioStatus))
    }

    var bdfDescription: String {
        let bus = (bdf >> 8) & 0xff
        let device = (bdf >> 3) & 0x1f
        let function = bdf & 0x7
        return String(format: "%02llX:%02llX.%llX", bus, device, function)
    }

    var pciIdentityDescription: String {
        let vendor = UInt16(deviceVendor & 0xffff)
        let device = UInt16((deviceVendor >> 16) & 0xffff)
        let subsystemVendor = UInt16(subsystem & 0xffff)
        let subsystemDevice = UInt16((subsystem >> 16) & 0xffff)
        return String(
            format: "%04X:%04X subsystem %04X:%04X",
            vendor, device, subsystemVendor, subsystemDevice
        )
    }
}

struct DriverBoundaryPreflightResult: Sendable {
    let packageAccepted: Bool
    let profileValid: Bool
    let captured: Bool
    let ioStatus: UInt64
    let boundaryStatus: UInt64
    let safeToProceed: Bool
    let activeMmuLock: Bool
    let prototypeBoundary: UInt64
    let effectiveBoundary: UInt64
    let mmuLockLow: UInt64
    let mmuLockHigh: UInt64

    var ioStatusDescription: String {
        String(format: "0x%08X", UInt32(truncatingIfNeeded: ioStatus))
    }

    var boundaryDescription: String {
        let names = [
            "ok", "invalid-profile", "mmu-lock-unavailable",
            "mmu-lock-unreadable", "rebuild-required"
        ]
        let index = Int(boundaryStatus)
        return index >= 0 && index < names.count
            ? names[index]
            : "unknown(\(boundaryStatus))"
    }
}

struct DriverColdBootSessionResult: Sendable {
    let ready: Bool
    let sessionStatus: UInt64
    let profileStatus: UInt64
    let planStatus: UInt64
    let packageResolveStatus: UInt64
    let bootResolveStatus: UInt64
    let ioStatus: UInt64
    let failedGeneratedIndex: UInt64
    let totalLogicalBytes: UInt64
    let totalAllocationBytes: UInt64
    let totalPages: UInt64
    let queueBacking: UInt64
    let cachedArguments: UInt64
    let libosInitArguments: UInt64
    let wprMetadata: UInt64
    let radix3FirmwareRoot: UInt64
    let firmwareSignature: UInt64
    let gspBootloader: UInt64
    let frtsFwsecOffset: UInt64
    let sec2BooterOffset: UInt64
    let bootPhaseCount: UInt64
    let executableWithCurrentCore: Bool
    let boundaryStatus: UInt64
    let boundaryRebuilt: Bool
    let prototypeBoundary: UInt64
    let effectiveBoundary: UInt64
    let activeMmuLock: Bool

    var sessionDescription: String {
        let names = [
            "idle", "ok", "bad-argument", "package-not-ready", "package-mismatch",
            "profile-rejected", "plan-rejected", "package-summary-rejected",
            "generated-allocation-failed", "page-address-allocation-failed",
            "page-address-validation-failed", "generated-layout-rejected",
            "address-resolve-failed", "system-info-failed", "artifact-build-failed",
            "artifact-population-failed", "sequence-rejected", "boundary-rejected"
        ]
        let index = Int(sessionStatus)
        return index >= 0 && index < names.count ? names[index] : "unknown(\(sessionStatus))"
    }

    var profileDescription: String {
        let names = [
            "ok", "package-not-verified", "unsupported-target", "invalid-vram",
            "missing-section", "section-too-large", "invalid-metadata", "manifest-rejected",
            "invalid-reserved-boundary"
        ]
        let index = Int(profileStatus)
        return index >= 0 && index < names.count ? names[index] : "unknown(\(profileStatus))"
    }

    var planDescription: String {
        let names = [
            "ok", "invalid-profile", "invalid-manifest", "size-overflow",
            "framebuffer-scratch-underflow", "frts-placement-mismatch"
        ]
        let index = Int(planStatus)
        return index >= 0 && index < names.count ? names[index] : "unknown(\(planStatus))"
    }

    var packageResolveDescription: String {
        let names = [
            "ok", "bad-plan", "wrong-section-count", "section-mismatch",
            "bad-allocation-size", "bad-page-count", "bad-page-address",
            "linear-layout-rejected"
        ]
        let index = Int(packageResolveStatus)
        return index >= 0 && index < names.count ? names[index] : "unknown(\(packageResolveStatus))"
    }

    var bootResolveDescription: String {
        let names = [
            "ok", "bad-profile", "bad-preparation-plan", "bad-staged-package-summary",
            "wrong-generated-count", "generated-kind-mismatch",
            "generated-page-count-mismatch", "generated-page-address-invalid",
            "generated-linear-layout-rejected", "package-reuse-missing",
            "package-reuse-mismatch"
        ]
        let index = Int(bootResolveStatus)
        return index >= 0 && index < names.count ? names[index] : "unknown(\(bootResolveStatus))"
    }

    var ioStatusDescription: String {
        String(format: "0x%08X", UInt32(truncatingIfNeeded: ioStatus))
    }

    var failedGeneratedDescription: String {
        if failedGeneratedIndex == UInt64(UInt32.max) { return "—" }
        let names = [
            "queue backing", "cached arguments", "LIBOS init arguments", "WPR metadata",
            "Radix3 firmware", "LOGINIT", "LOGINTR", "LOGRM", "LOGMNOC", "LOGKRNL"
        ]
        let index = Int(failedGeneratedIndex)
        return index >= 0 && index < names.count ? names[index] : "index \(failedGeneratedIndex)"
    }

    var boundaryDescription: String {
        let names = [
            "ok", "invalid-profile", "mmu-lock-unavailable",
            "mmu-lock-unreadable", "rebuild-required"
        ]
        let index = Int(boundaryStatus)
        return index >= 0 && index < names.count ? names[index] : "unknown(\(boundaryStatus))"
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

private func describeHex(_ value: UInt64) -> String {
    String(format: "0x%016llX", value)
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

    func callScalarOutputMethod(
        selector: UInt32,
        expectedOutputCount: UInt32
    ) throws -> [UInt64] {
        var output = [UInt64](repeating: 0, count: Int(expectedOutputCount))
        var outputCount = expectedOutputCount
        let callResult: kern_return_t = output.withUnsafeMutableBufferPointer { outputBuffer in
            IOConnectCallMethod(
                connection,
                selector,
                nil,
                0,
                nil,
                0,
                outputBuffer.baseAddress,
                &outputCount,
                nil,
                nil
            )
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

private func readSystemInfoWithDriver() throws -> DriverSystemInfoResult {
    let session = try RTXMacDriverSession.open()
    let output = try session.callScalarOutputMethod(
        selector: UserClientSelector.systemInfo,
        expectedOutputCount: 16
    )
    return DriverSystemInfoResult(
        available: output[0] != 0,
        ioStatus: output[1],
        bdf: output[2],
        deviceVendor: output[3],
        subsystem: output[4],
        revision: output[5],
        bar0Base: output[6],
        bar0Size: output[7],
        bar1Base: output[8],
        bar1Size: output[9],
        bar3Base: output[10],
        bar3Size: output[11],
        maxUserVa: output[12],
        pciConfigMirrorBase: output[13],
        pciConfigMirrorSize: output[14],
        passthrough: output[15] != 0
    )
}

private func checkLiveBoundaryWithDriver(
    _ data: Data
) throws -> DriverBoundaryPreflightResult {
    let session = try RTXMacDriverSession.open()
    let output = try session.callPackageMethod(
        data,
        selector: UserClientSelector.liveBoundary,
        expectedOutputCount: 11
    )
    return DriverBoundaryPreflightResult(
        packageAccepted: output[0] != 0,
        profileValid: output[1] != 0,
        captured: output[2] != 0,
        ioStatus: output[3],
        boundaryStatus: output[4],
        safeToProceed: output[5] != 0,
        activeMmuLock: output[6] != 0,
        prototypeBoundary: output[7],
        effectiveBoundary: output[8],
        mmuLockLow: output[9],
        mmuLockHigh: output[10]
    )
}

private func prepareColdBootSessionWithDriver(
    _ data: Data,
    session: RTXMacDriverSession
) throws -> DriverColdBootSessionResult {
    let output = try session.callPackageMethod(
        data,
        selector: UserClientSelector.prepareColdBootSession,
        expectedOutputCount: 27
    )
    return DriverColdBootSessionResult(
        ready: output[0] != 0,
        sessionStatus: output[1],
        profileStatus: output[2],
        planStatus: output[3],
        packageResolveStatus: output[4],
        bootResolveStatus: output[5],
        ioStatus: output[6],
        failedGeneratedIndex: output[7],
        totalLogicalBytes: output[8],
        totalAllocationBytes: output[9],
        totalPages: output[10],
        queueBacking: output[11],
        cachedArguments: output[12],
        libosInitArguments: output[13],
        wprMetadata: output[14],
        radix3FirmwareRoot: output[15],
        firmwareSignature: output[16],
        gspBootloader: output[17],
        frtsFwsecOffset: output[18],
        sec2BooterOffset: output[19],
        bootPhaseCount: output[20],
        executableWithCurrentCore: output[21] != 0,
        boundaryStatus: output[22],
        boundaryRebuilt: output[23] != 0,
        prototypeBoundary: output[24],
        effectiveBoundary: output[25],
        activeMmuLock: output[26] != 0
    )
}

@MainActor
final class ExtensionManager: NSObject, ObservableObject, OSSystemExtensionRequestDelegate {
    @Published var status = "Driver not activated by this app yet."
    @Published var systemInfoStatus = "System info has not been read."
    @Published var systemInfo: DriverSystemInfoResult?
    @Published var readingSystemInfo = false
    @Published var packageStatus = "No package selected."
    @Published var packageName = "—"
    @Published var validation: DriverValidationResult?
    @Published var validating = false
    @Published var canStage = false
    @Published var staging = false
    @Published var stagingStatus = "Package is not staged."
    @Published var stagingResult: DriverStagingResult?
    @Published var checkingBoundary = false
    @Published var boundaryStatus = "Live MMU boundary has not been checked."
    @Published var boundaryResult: DriverBoundaryPreflightResult?
    @Published var preparingBootSession = false
    @Published var bootSessionStatus = "Cold boot session has not been prepared."
    @Published var bootSessionResult: DriverColdBootSessionResult?

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

    func readSystemInfo() {
        readingSystemInfo = true
        systemInfo = nil
        systemInfoStatus = "Reading PCI BARs and GSP system-info inputs…"

        Task.detached(priority: .userInitiated) {
            do {
                let result = try readSystemInfoWithDriver()
                await MainActor.run {
                    self.readingSystemInfo = false
                    self.systemInfo = result
                    self.systemInfoStatus = result.available
                        ? "Read-only system info collected."
                        : "System-info collection failed in DriverKit: \(result.ioStatusDescription)."
                }
            } catch {
                await MainActor.run {
                    self.readingSystemInfo = false
                    self.systemInfo = nil
                    self.systemInfoStatus = error.localizedDescription
                }
            }
        }
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
        boundaryResult = nil
        boundaryStatus = "Live MMU boundary has not been checked."
        bootSessionResult = nil
        bootSessionStatus = "Cold boot session has not been prepared."

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
        bootSessionResult = nil
        bootSessionStatus = "Cold boot session has not been prepared."

        Task.detached(priority: .userInitiated) {
            do {
                let (result, session) = try stagePackageWithDriver(data)
                await MainActor.run {
                    self.staging = false
                    self.stagingResult = result
                    if result.ready {
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

    func prepareSelectedColdBootSession() {
        guard let data = selectedPackageData,
              let session = stagedSession,
              stagingResult?.ready == true else {
            bootSessionStatus = "Stage the accepted .rtxpkg before preparing the cold boot session."
            return
        }

        preparingBootSession = true
        bootSessionResult = nil
        bootSessionStatus = "Allocating generated DMA buffers and constructing cold boot artifacts…"

        Task.detached(priority: .userInitiated) {
            do {
                let result = try prepareColdBootSessionWithDriver(data, session: session)
                await MainActor.run {
                    self.preparingBootSession = false
                    self.bootSessionResult = result
                    self.bootSessionStatus = result.ready
                        ? (result.boundaryRebuilt
                            ? "Cold host-memory graph was rebuilt around the live MMU boundary and retained. Hardware execution remains disabled."
                            : "Cold host-memory graph is retained and internally consistent. Hardware execution remains disabled.")
                        : "Cold boot preparation failed: \(result.sessionDescription), I/O \(result.ioStatusDescription)."
                }
            } catch {
                await MainActor.run {
                    self.preparingBootSession = false
                    self.bootSessionResult = nil
                    self.bootSessionStatus = error.localizedDescription
                }
            }
        }
    }

    func checkSelectedPackageBoundary() {
        guard let data = selectedPackageData, validation?.accepted == true else {
            boundaryStatus = "Validate an accepted .rtxpkg before checking the live boundary."
            return
        }

        checkingBoundary = true
        boundaryResult = nil
        boundaryStatus = "Reading the GA10x MMU-lock registers and comparing the reserved boundary…"

        Task.detached(priority: .userInitiated) {
            do {
                let result = try checkLiveBoundaryWithDriver(data)
                await MainActor.run {
                    self.checkingBoundary = false
                    self.boundaryResult = result
                    if result.safeToProceed {
                        self.boundaryStatus = "Read-only boundary preflight passed."
                    } else if result.boundaryDescription == "rebuild-required" {
                        self.boundaryStatus = "The live MMU lock lowers the reserved boundary. Rebuild the boot layout before any write is allowed."
                    } else {
                        self.boundaryStatus = "Read-only boundary preflight did not pass: \(result.boundaryDescription), I/O \(result.ioStatusDescription)."
                    }
                }
            } catch {
                await MainActor.run {
                    self.checkingBoundary = false
                    self.boundaryResult = nil
                    self.boundaryStatus = error.localizedDescription
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
        boundaryResult = nil
        boundaryStatus = "Live MMU boundary has not been checked."
        bootSessionResult = nil
        bootSessionStatus = "Cold boot session has not been prepared."
    }

    func makeDiagnosticReportDocument() -> RTXMacDiagnosticDocument {
        var report: [String: Any] = [
            "schema": "rtxmac-read-only-diagnostics-v1",
            "generated_at": ISO8601DateFormatter().string(from: Date()),
            "safety_boundary": "No GPU reset, PCI command change, MMIO/PRAMIN write, DMA execution, Falcon execution, or GSP start was requested by this report.",
            "extension_status": status,
            "system_info_status": systemInfoStatus,
            "package_name": packageName,
            "package_status": packageStatus,
            "staging_status": stagingStatus,
            "boot_session_status": bootSessionStatus,
            "boundary_status": boundaryStatus,
        ]

        if let info = systemInfo {
            report["system_info"] = [
                "available": info.available,
                "io_status": info.ioStatusDescription,
                "pci_bdf": info.bdfDescription,
                "pci_identity": info.pciIdentityDescription,
                "revision": String(format: "0x%02llX", info.revision),
                "bar0_base": describeHex(info.bar0Base),
                "bar0_size": info.bar0Size,
                "bar1_base": describeHex(info.bar1Base),
                "bar1_size": info.bar1Size,
                "bar3_base": describeHex(info.bar3Base),
                "bar3_size": info.bar3Size,
                "max_user_va": describeHex(info.maxUserVa),
                "pci_config_mirror_base": describeHex(info.pciConfigMirrorBase),
                "pci_config_mirror_size": info.pciConfigMirrorSize,
                "passthrough": info.passthrough,
            ] as [String: Any]
        }

        if let result = validation {
            report["package_validation"] = [
                "accepted": result.accepted,
                "parse_status": result.parseDescription,
                "semantic_status": result.semanticDescription,
                "package_bytes": result.packageBytes,
                "live_pci_identity": describePCIIdentity(result.liveIdentity),
                "package_pci_identity": describePCIIdentity(result.packageIdentity),
                "driver_max_package_bytes": result.driverMaxPackageBytes,
            ] as [String: Any]
        }

        if let result = stagingResult {
            report["cold_sysram_staging"] = [
                "ready": result.ready,
                "stage_status": result.stageDescription,
                "plan_status": result.planDescription,
                "io_status": result.ioStatusDescription,
                "failed_section": result.failedSectionDescription,
                "logical_bytes": result.totalLogicalBytes,
                "allocation_bytes": result.totalAllocationBytes,
                "page_count": result.totalPages,
                "first_dma_pages": result.firstDmaPages.map(describeDmaAddress),
            ] as [String: Any]
        }

        if let result = boundaryResult {
            report["mmu_reserved_boundary"] = [
                "package_accepted": result.packageAccepted,
                "profile_valid": result.profileValid,
                "registers_captured": result.captured,
                "io_status": result.ioStatusDescription,
                "decision": result.boundaryDescription,
                "safe_to_proceed": result.safeToProceed,
                "active_mmu_lock": result.activeMmuLock,
                "prototype_boundary": describeHex(result.prototypeBoundary),
                "effective_boundary": describeHex(result.effectiveBoundary),
                "mmu_lock_low": describeHex(result.mmuLockLow),
                "mmu_lock_high": describeHex(result.mmuLockHigh),
            ] as [String: Any]
        }

        if let result = bootSessionResult {
            report["cold_boot_session"] = [
                "ready": result.ready,
                "session_status": result.sessionDescription,
                "profile_status": result.profileDescription,
                "preparation_plan_status": result.planDescription,
                "package_dma_resolve_status": result.packageResolveDescription,
                "boot_address_resolve_status": result.bootResolveDescription,
                "io_status": result.ioStatusDescription,
                "failed_generated_buffer": result.failedGeneratedDescription,
                "logical_bytes": result.totalLogicalBytes,
                "allocation_bytes": result.totalAllocationBytes,
                "page_count": result.totalPages,
                "queue_backing": describeDmaAddress(result.queueBacking),
                "cached_arguments": describeDmaAddress(result.cachedArguments),
                "libos_init_arguments": describeDmaAddress(result.libosInitArguments),
                "wpr_metadata": describeDmaAddress(result.wprMetadata),
                "radix3_firmware_root": describeDmaAddress(result.radix3FirmwareRoot),
                "firmware_signature": describeDmaAddress(result.firmwareSignature),
                "gsp_bootloader": describeDmaAddress(result.gspBootloader),
                "frts_fwsec_vram_offset": describeHex(result.frtsFwsecOffset),
                "sec2_booter_vram_offset": describeHex(result.sec2BooterOffset),
                "planned_boot_phases": result.bootPhaseCount,
                "sequence_supported_by_current_core": result.executableWithCurrentCore,
                "live_boundary_status": result.boundaryDescription,
                "layout_rebuilt_for_live_boundary": result.boundaryRebuilt,
                "active_mmu_lock": result.activeMmuLock,
                "prototype_boundary": describeHex(result.prototypeBoundary),
                "effective_boundary": describeHex(result.effectiveBoundary),
            ] as [String: Any]
        }

        let data = (try? JSONSerialization.data(
            withJSONObject: report,
            options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
        )) ?? Data("{}\n".utf8)
        return RTXMacDiagnosticDocument(data: data)
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
    @State private var exportingDiagnostics = false
    @State private var diagnosticDocument = RTXMacDiagnosticDocument()

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
                        HStack {
                            Button("Activate read-only driver") {
                                extensions.activate()
                            }
                            .buttonStyle(.borderedProminent)

                            Button(extensions.readingSystemInfo ? "Reading system info…" : "Read PCI / GSP system info") {
                                extensions.readSystemInfo()
                            }
                            .disabled(extensions.readingSystemInfo)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                GroupBox("Read-only PCI / GSP system info") {
                    VStack(alignment: .leading, spacing: 10) {
                        Text(extensions.systemInfoStatus)
                            .textSelection(.enabled)

                        if let info = extensions.systemInfo {
                            Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 6) {
                                GridRow { Text("Available"); Text(info.available ? "YES" : "NO") }
                                GridRow { Text("Driver I/O"); Text(info.ioStatusDescription) }
                                if info.available {
                                    GridRow { Text("PCI BDF"); Text(info.bdfDescription) }
                                    GridRow { Text("PCI identity"); Text(info.pciIdentityDescription) }
                                    GridRow { Text("Revision"); Text(String(format: "0x%02llX", info.revision)) }
                                    GridRow { Text("BAR0"); Text("\(describeHex(info.bar0Base)) / \(info.bar0Size) bytes") }
                                    GridRow { Text("BAR1"); Text("\(describeHex(info.bar1Base)) / \(info.bar1Size) bytes") }
                                    GridRow { Text("BAR3"); Text("\(describeHex(info.bar3Base)) / \(info.bar3Size) bytes") }
                                    GridRow { Text("Max user VA"); Text(describeHex(info.maxUserVa)) }
                                    GridRow { Text("PCI config mirror"); Text("\(describeHex(info.pciConfigMirrorBase)) / \(info.pciConfigMirrorSize) bytes") }
                                    GridRow { Text("Passthrough"); Text(info.passthrough ? "YES" : "NO") }
                                }
                            }
                            .font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                        }

                        Text("This path only reads PCI configuration/BAR metadata. It does not map BARs for writes, reset the GPU, or execute firmware.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                GroupBox("Validated boot package") {
                    VStack(alignment: .leading, spacing: 10) {
                        HStack {
                            Button(extensions.validating ? "Validating…" : "Select .rtxpkg and validate") {
                                importingPackage = true
                            }
                            .disabled(
                                extensions.validating || extensions.staging ||
                                extensions.preparingBootSession
                            )

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
                        .disabled(
                            !extensions.canStage || extensions.staging || extensions.validating ||
                            extensions.preparingBootSession
                        )

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

                GroupBox("Retained cold boot preparation") {
                    VStack(alignment: .leading, spacing: 10) {
                        Button(
                            extensions.preparingBootSession
                                ? "Preparing cold boot session…"
                                : "Build complete cold host-memory graph"
                        ) {
                            extensions.prepareSelectedColdBootSession()
                        }
                        .disabled(
                            extensions.stagingResult?.ready != true ||
                            extensions.preparingBootSession || extensions.staging ||
                            extensions.validating
                        )

                        Text(extensions.bootSessionStatus)
                            .textSelection(.enabled)

                        if let result = extensions.bootSessionResult {
                            Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 6) {
                                GridRow { Text("Ready"); Text(result.ready ? "YES" : "NO") }
                                GridRow { Text("Session status"); Text(result.sessionDescription) }
                                GridRow { Text("GA104 profile"); Text(result.profileDescription) }
                                GridRow { Text("Preparation plan"); Text(result.planDescription) }
                                GridRow { Text("Package DMA resolve"); Text(result.packageResolveDescription) }
                                GridRow { Text("Boot address resolve"); Text(result.bootResolveDescription) }
                                GridRow { Text("Driver I/O"); Text(result.ioStatusDescription) }
                                GridRow { Text("Failed generated buffer"); Text(result.failedGeneratedDescription) }
                                GridRow { Text("Logical / allocated bytes"); Text("\(result.totalLogicalBytes) / \(result.totalAllocationBytes)") }
                                GridRow { Text("Generated DMA pages"); Text("\(result.totalPages)") }
                                GridRow { Text("Queue backing"); Text(describeDmaAddress(result.queueBacking)) }
                                GridRow { Text("Cached arguments"); Text(describeDmaAddress(result.cachedArguments)) }
                                GridRow { Text("LIBOS init arguments"); Text(describeDmaAddress(result.libosInitArguments)) }
                                GridRow { Text("WPR metadata"); Text(describeDmaAddress(result.wprMetadata)) }
                                GridRow { Text("Radix3 firmware root"); Text(describeDmaAddress(result.radix3FirmwareRoot)) }
                                GridRow { Text("Staged signature"); Text(describeDmaAddress(result.firmwareSignature)) }
                                GridRow { Text("Staged bootloader"); Text(describeDmaAddress(result.gspBootloader)) }
                                GridRow { Text("FWSEC VRAM offset"); Text(describeHex(result.frtsFwsecOffset)) }
                                GridRow { Text("SEC2 VRAM offset"); Text(describeHex(result.sec2BooterOffset)) }
                                GridRow { Text("Planned phases"); Text("\(result.bootPhaseCount)") }
                                GridRow { Text("Core sequence complete"); Text(result.executableWithCurrentCore ? "YES" : "NO") }
                                GridRow { Text("Live boundary input"); Text(result.boundaryDescription) }
                                GridRow { Text("Layout rebuilt"); Text(result.boundaryRebuilt ? "YES" : "NO") }
                                GridRow { Text("Active MMU lock"); Text(result.activeMmuLock ? "YES" : "NO") }
                                GridRow { Text("Prototype boundary"); Text(describeHex(result.prototypeBoundary)) }
                                GridRow { Text("Effective boundary"); Text(describeHex(result.effectiveBoundary)) }
                            }
                            .font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                        }

                        Text("This first reads the allow-listed MMU boundary, then allocates and fills queue, arguments, metadata, Radix3, and log buffers around the effective live layout. Ready means internally consistent—not armed. It performs no PCI command change, MMIO/PRAMIN write, reset, Falcon execution, or GSP start.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                GroupBox("Read-only MMU reserved-boundary preflight") {
                    VStack(alignment: .leading, spacing: 10) {
                        Button(extensions.checkingBoundary ? "Checking boundary…" : "Check live MMU boundary") {
                            extensions.checkSelectedPackageBoundary()
                        }
                        .disabled(
                            !extensions.canStage || extensions.checkingBoundary ||
                            extensions.validating || extensions.staging
                        )

                        Text(extensions.boundaryStatus)
                            .textSelection(.enabled)

                        if let result = extensions.boundaryResult {
                            Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 6) {
                                GridRow { Text("Package accepted"); Text(result.packageAccepted ? "YES" : "NO") }
                                GridRow { Text("Boundary profile"); Text(result.profileValid ? "VALID" : "INVALID") }
                                GridRow { Text("Live registers captured"); Text(result.captured ? "YES" : "NO") }
                                GridRow { Text("Driver I/O"); Text(result.ioStatusDescription) }
                                GridRow { Text("Decision"); Text(result.boundaryDescription) }
                                GridRow { Text("Safe for later write stage"); Text(result.safeToProceed ? "YES" : "NO") }
                                GridRow { Text("Active MMU lock"); Text(result.activeMmuLock ? "YES" : "NO") }
                                GridRow { Text("Prototype boundary"); Text(describeHex(result.prototypeBoundary)) }
                                GridRow { Text("Effective boundary"); Text(describeHex(result.effectiveBoundary)) }
                                GridRow { Text("MMU lock low"); Text(describeHex(result.mmuLockLow)) }
                                GridRow { Text("MMU lock high"); Text(describeHex(result.mmuLockHigh)) }
                            }
                            .font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                        }

                        Text("This maps one BAR0 page for volatile 32-bit reads only. It performs no reset, MMIO write, PRAMIN write, PCI command change, DMA execution, Falcon execution, or GSP start.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                GroupBox("Diagnostic handoff") {
                    VStack(alignment: .leading, spacing: 10) {
                        Button("Export read-only diagnostics JSON") {
                            diagnosticDocument = extensions.makeDiagnosticReportDocument()
                            exportingDiagnostics = true
                        }

                        Text("Save this JSON, then pass it to scripts/macos/collect-diagnostics.sh so the host results and macOS logs are captured in one ZIP for analysis from Windows.")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                Text("Cold staging allocates and prepares host SYSRAM only. It does not reset the GPU, write BAR0/PRAMIN, change clocks or power, execute Falcon firmware, or start GSP-RM.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(24)
        }
        .frame(minWidth: 820, minHeight: 700)
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
        .fileExporter(
            isPresented: $exportingDiagnostics,
            document: diagnosticDocument,
            contentType: .json,
            defaultFilename: "RTXMac-Diagnostics"
        ) { _ in }
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
