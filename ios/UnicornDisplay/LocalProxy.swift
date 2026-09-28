import Foundation
import Network

// Serves the bundled screens on localhost and forwards everything else
// (frames, settings, models, the SD card page) to the board.
final class LocalProxy: @unchecked Sendable {
    private let webRoot: URL
    private let session: URLSession
    private let queue = DispatchQueue(label: "unicorn.proxy")
    private var listener: NWListener?
    private let boardHost: String
    private let maxBody = 8 * 1024 * 1024

    init(webRoot: URL, boardHost: String) {
        self.webRoot = webRoot
        self.boardHost = boardHost
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 30
        config.timeoutIntervalForResource = 120
        config.waitsForConnectivity = false
        session = URLSession(configuration: config)
    }

    func start() throws -> UInt16 {
        let params = NWParameters.tcp
        params.requiredInterfaceType = .loopback
        params.allowLocalEndpointReuse = true
        let listener = try NWListener(using: params, on: .any)
        let ready = DispatchSemaphore(value: 0)
        var failed: Error?
        listener.stateUpdateHandler = { state in
            switch state {
            case .ready:
                ready.signal()
            case .failed(let error):
                failed = error
                ready.signal()
            default:
                break
            }
        }
        listener.newConnectionHandler = { [weak self] connection in
            connection.start(queue: self?.queue ?? .main)
            Task { await self?.serve(connection) }
        }
        listener.start(queue: queue)
        guard ready.wait(timeout: .now() + 3) == .success else {
            listener.cancel()
            throw ProxyError.failed("The local server did not start.")
        }
        if let failed {
            listener.cancel()
            throw failed
        }
        guard let port = listener.port?.rawValue, port != 0 else {
            listener.cancel()
            throw ProxyError.failed("The local server did not get a port.")
        }
        self.listener = listener
        return port
    }

    func stop() {
        listener?.cancel()
        listener = nil
        session.invalidateAndCancel()
    }

    private func serve(_ connection: NWConnection) async {
        let io = ConnIO(connection)
        do {
            let request = try await readRequest(io)
            if let file = staticFile(request.target) {
                let data = try Data(contentsOf: file)
                try await write(io, status: 200, type: mime(file), body: request.method == "HEAD" ? Data() : data)
            } else {
                try await forward(request, io)
            }
        } catch {
            try? await write(io, status: 502, type: "text/plain; charset=utf-8", body: Data("board unavailable".utf8))
        }
        connection.cancel()
    }

    private func readRequest(_ io: ConnIO) async throws -> ProxyRequest {
        let head = try await io.read(until: Data("\r\n\r\n".utf8), max: 16 * 1024)
        guard let headerText = String(data: head, encoding: .utf8) else { throw ProxyError.badRequest }
        let lines = headerText.split(separator: "\r\n", omittingEmptySubsequences: false)
        guard let first = lines.first else { throw ProxyError.badRequest }
        let parts = first.split(separator: " ")
        guard parts.count >= 2 else { throw ProxyError.badRequest }
        var headers: [String: String] = [:]
        for line in lines.dropFirst() where line.contains(":") {
            let pair = line.split(separator: ":", maxSplits: 1)
            headers[pair[0].lowercased()] = pair[1].trimmingCharacters(in: .whitespaces)
        }
        let length = min(Int(headers["content-length"] ?? "") ?? 0, maxBody)
        let body = length > 0 ? try await io.read(count: length) : Data()
        return ProxyRequest(method: String(parts[0]), target: String(parts[1]), headers: headers, body: body)
    }

    private func staticFile(_ target: String) -> URL? {
        let path = target.split(separator: "?", maxSplits: 1).first.map(String.init) ?? "/"
        let relative = path == "/" ? "index.html" : String(path.drop(while: { $0 == "/" }))
        if relative.isEmpty || relative.contains("..") || relative.hasPrefix("/") { return nil }
        let url = webRoot.appendingPathComponent(relative).standardizedFileURL
        let root = webRoot.standardizedFileURL.path
        let file = url.path
        guard file == root || file.hasPrefix(root + "/") else { return nil }
        var isDir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: file, isDirectory: &isDir), !isDir.boolValue else { return nil }
        return url
    }

    private func forward(_ request: ProxyRequest, _ io: ConnIO) async throws {
        guard let url = URL(string: "http://\(boardHost)\(request.target.hasPrefix("/") ? "" : "/")\(request.target)") else {
            throw ProxyError.badRequest
        }
        var outbound = URLRequest(url: url)
        outbound.httpMethod = request.method
        if !request.body.isEmpty { outbound.httpBody = request.body }
        if let type = request.headers["content-type"] {
            outbound.setValue(type, forHTTPHeaderField: "Content-Type")
        }
        let (data, response) = try await session.data(for: outbound)
        let status = (response as? HTTPURLResponse)?.statusCode ?? 502
        let type = (response as? HTTPURLResponse)?.value(forHTTPHeaderField: "Content-Type") ?? "application/octet-stream"
        try await write(io, status: status, type: type, body: request.method == "HEAD" ? Data() : data)
    }

    private func write(_ io: ConnIO, status: Int, type: String, body: Data) async throws {
        let reason = Self.reasons[status] ?? "OK"
        let head = "HTTP/1.1 \(status) \(reason)\r\nContent-Type: \(type)\r\nContent-Length: \(body.count)\r\nConnection: close\r\n\r\n"
        var bytes = Data(head.utf8)
        bytes.append(body)
        try await io.write(bytes)
    }

    private func mime(_ url: URL) -> String {
        switch url.pathExtension.lowercased() {
        case "html": return "text/html; charset=utf-8"
        case "css": return "text/css; charset=utf-8"
        case "js": return "text/javascript; charset=utf-8"
        case "png": return "image/png"
        case "json": return "application/json"
        default: return "application/octet-stream"
        }
    }

    private static let reasons = [
        200: "OK", 204: "No Content", 400: "Bad Request", 404: "Not Found",
        413: "Payload Too Large", 500: "Error", 502: "Bad Gateway", 503: "Unavailable",
    ]
}

private struct ProxyRequest {
    var method: String
    var target: String
    var headers: [String: String]
    var body: Data
}

enum ProxyError: LocalizedError {
    case failed(String)
    case badRequest
    var errorDescription: String? {
        switch self {
        case .failed(let message): return message
        case .badRequest: return "Bad request."
        }
    }
}

private final class ConnIO: @unchecked Sendable {
    let connection: NWConnection
    private var stash = Data()

    init(_ connection: NWConnection) { self.connection = connection }

    func read(until marker: Data, max: Int) async throws -> Data {
        while true {
            if let range = stash.range(of: marker) {
                let end = range.upperBound
                let out = stash.subdata(in: 0..<end)
                stash.removeSubrange(0..<end)
                return out
            }
            if stash.count > max { throw ProxyError.badRequest }
            let chunk = try await receive()
            if chunk.isEmpty { throw ProxyError.badRequest }
            stash.append(chunk)
        }
    }

    func read(count: Int) async throws -> Data {
        while stash.count < count {
            let chunk = try await receive()
            if chunk.isEmpty { throw ProxyError.badRequest }
            stash.append(chunk)
        }
        let out = stash.subdata(in: 0..<count)
        stash.removeSubrange(0..<count)
        return out
    }

    func write(_ data: Data) async throws {
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
            connection.send(content: data, completion: .contentProcessed { error in
                if let error { cont.resume(throwing: error) } else { cont.resume() }
            })
        }
    }

    private func receive() async throws -> Data {
        try await withCheckedThrowingContinuation { cont in
            connection.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { data, _, isComplete, error in
                if let error {
                    cont.resume(throwing: error)
                    return
                }
                if let data, !data.isEmpty {
                    cont.resume(returning: data)
                    return
                }
                if isComplete {
                    cont.resume(returning: Data())
                    return
                }
                cont.resume(returning: Data())
            }
        }
    }
}
