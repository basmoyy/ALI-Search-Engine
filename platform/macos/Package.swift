// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "ALI",
    platforms: [.macOS(.v13)],
    products: [
        .executable(name: "ALI", targets: ["ALI"]),
    ],
    targets: [
        // C bridge target — wraps ali.h so Swift can call the C API
        .systemLibrary(
            name: "AliC",
            path: "Sources/AliC",
            pkgConfig: nil,
            providers: nil
        ),
        // Main Swift application
        .executableTarget(
            name: "ALI",
            dependencies: [],
            path: "Sources/ALI",
            resources: [
                .copy("../../Resources/AppIcon.icns"),
            ],
            swiftSettings: [
                .unsafeFlags(["-I../../interfaces/c_api"])
            ],
            linkerSettings: [
                .unsafeFlags([
                    // Link the pre-built ali_core and ali_capi static libs
                    "-L../../build/core",
                    "-L../../build/interfaces/c_api",
                    "-lali_capi",
                    "-lali_core",
                    "-lsqlite3",
                    "-lssl",
                    "-lcrypto",
                    "-L/opt/homebrew/opt/openssl/lib",
                ])
            ]
        ),
    ]
)
