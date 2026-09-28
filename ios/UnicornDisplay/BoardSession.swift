import Foundation

@MainActor
final class BoardSession: ObservableObject {
    @Published var host: String
    @Published var pageURL: URL?
    @Published var status = ""
    @Published var busy = false

    private var proxy: LocalProxy?
    private let defaultsKey = "boardHost"

    init() {
        host = UserDefaults.standard.string(forKey: "boardHost") ?? "192.168.4.1"
    }

    var normalizedHost: String {
        var text = host.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.hasPrefix("http://") { text.removeFirst("http://".count) }
        if text.hasPrefix("https://") { text.removeFirst("https://".count) }
        while text.hasSuffix("/") { text.removeLast() }
        if let slash = text.firstIndex(of: "/") { text = String(text[..<slash]) }
        return text.isEmpty ? "192.168.4.1" : text
    }

    func connect() async {
        let board = normalizedHost
        guard !board.contains(" "), let settings = URL(string: "http://\(board)/settings") else {
            status = "That address doesn't look right."
            return
        }
        busy = true
        status = ""
        defer { busy = false }
        var probe = URLRequest(url: settings)
        probe.timeoutInterval = 5
        do {
            let (_, response) = try await URLSession.shared.data(for: probe)
            guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
                status = "The board at \(board) answered, but not with its settings."
                return
            }
        } catch {
            status = "Can't reach the board at \(board). Join Unicorn-Display (the address is 192.168.4.1), or enter the address from the serial log. If the phone asks for local network access, allow it."
            return
        }
        guard let root = Bundle.main.url(forResource: "web", withExtension: nil) else {
            status = "This app is missing its screens."
            return
        }
        let server = LocalProxy(webRoot: root, boardHost: board)
        do {
            let port = try server.start()
            proxy = server
            host = board
            UserDefaults.standard.set(board, forKey: defaultsKey)
            pageURL = URL(string: "http://127.0.0.1:\(port)/")
        } catch {
            server.stop()
            status = "Couldn't open the app screens. \(error.localizedDescription)"
        }
    }

    func disconnect() {
        let board = normalizedHost
        proxy?.stop()
        proxy = nil
        pageURL = nil
        var stop = URLRequest(url: URL(string: "http://\(board)/stop")!)
        stop.httpMethod = "POST"
        stop.timeoutInterval = 2
        Task { _ = try? await URLSession.shared.data(for: stop) }
    }
}
