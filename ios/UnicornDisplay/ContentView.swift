import SwiftUI

struct ContentView: View {
    @StateObject private var session = BoardSession()

    var body: some View {
        Group {
            if let page = session.pageURL {
                DisplayWebView(url: page)
                    .ignoresSafeArea(edges: .bottom)
                    .safeAreaInset(edge: .top, spacing: 0) {
                        HStack {
                            Text(session.normalizedHost)
                                .foregroundStyle(UnicornColor.dim)
                            Spacer()
                            Button("Disconnect", action: session.disconnect)
                                .foregroundStyle(UnicornColor.accent)
                        }
                        .font(.subheadline.weight(.semibold))
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                        .background(UnicornColor.bg)
                    }
            } else {
                connect
            }
        }
        .preferredColorScheme(.dark)
    }

    private var connect: some View {
        ZStack {
            UnicornColor.bg.ignoresSafeArea()
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    Text("Unicorn Display")
                        .font(.largeTitle.bold())
                    Text("Animals, wildlife, the fairy garden, NES, and video run on this phone. Connect, and Send to display puts the picture on the board.")
                        .foregroundStyle(UnicornColor.dim)
                    TextField("192.168.4.1", text: $session.host)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .keyboardType(.URL)
                        .padding(12)
                        .background(Color.white.opacity(0.06))
                        .clipShape(RoundedRectangle(cornerRadius: 10))
                    Button {
                        Task { await session.connect() }
                    } label: {
                        Text(session.busy ? "Connecting…" : "Connect")
                            .fontWeight(.semibold)
                            .foregroundStyle(Color(red: 0.10, green: 0.05, blue: 0.07))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 12)
                            .background(UnicornColor.accent)
                            .clipShape(RoundedRectangle(cornerRadius: 10))
                    }
                    .disabled(session.busy)
                    Text("On the board's own Wi-Fi the address is 192.168.4.1. On your network it is unicorn.local, or whatever the serial log prints. Allow local network access when the phone asks.")
                        .font(.footnote)
                        .foregroundStyle(UnicornColor.dim)
                    if !session.status.isEmpty {
                        Text(session.status)
                            .foregroundStyle(Color(red: 1, green: 0.54, blue: 0.50))
                    }
                }
                .padding(24)
            }
        }
    }
}

enum UnicornColor {
    static let bg = Color(red: 14 / 255, green: 14 / 255, blue: 18 / 255)
    static let accent = Color(red: 240 / 255, green: 108 / 255, blue: 155 / 255)
    static let dim = Color(red: 154 / 255, green: 154 / 255, blue: 168 / 255)
}
