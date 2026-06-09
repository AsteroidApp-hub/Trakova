import Cocoa
import WebKit

// 使い方: svg2png <in.svg> <out.png> <size>
let args = CommandLine.arguments
guard args.count >= 3 else { FileHandle.standardError.write("usage: svg2png in.svg out.png [size]\n".data(using: .utf8)!); exit(2) }
let svgPath = args[1]
let outPath = args[2]
let size = args.count > 3 ? Int(args[3])! : 1024

let app = NSApplication.shared
app.setActivationPolicy(.accessory)

final class Snapper: NSObject, WKNavigationDelegate {
    let out: String, size: Int
    init(out: String, size: Int) { self.out = out; self.size = size }
    func webView(_ webView: WKWebView, didFinish nav: WKNavigation!) {
        let cfg = WKSnapshotConfiguration()
        cfg.rect = CGRect(x: 0, y: 0, width: size, height: size)
        // レイアウト確定を待ってからスナップショット
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) {
            webView.takeSnapshot(with: cfg) { image, error in
                guard let image = image,
                      let tiff = image.tiffRepresentation,
                      let rep = NSBitmapImageRep(data: tiff),
                      let png = rep.representation(using: .png, properties: [:]) else {
                    FileHandle.standardError.write("snapshot failed: \(String(describing: error))\n".data(using: .utf8)!)
                    exit(1)
                }
                do { try png.write(to: URL(fileURLWithPath: self.out)) }
                catch { FileHandle.standardError.write("write failed: \(error)\n".data(using: .utf8)!); exit(1) }
                print("wrote \(self.out) (\(rep.pixelsWide)x\(rep.pixelsHigh))")
                exit(0)
            }
        }
    }
    func webView(_ webView: WKWebView, didFail nav: WKNavigation!, withError error: Error) {
        FileHandle.standardError.write("nav fail: \(error)\n".data(using: .utf8)!); exit(1)
    }
    func webView(_ webView: WKWebView, didFailProvisionalNavigation nav: WKNavigation!, withError error: Error) {
        FileHandle.standardError.write("provisional fail: \(error)\n".data(using: .utf8)!); exit(1)
    }
}

let frame = CGRect(x: 0, y: 0, width: size, height: size)
let config = WKWebViewConfiguration()
let webView = WKWebView(frame: frame, configuration: config)
webView.setValue(false, forKey: "drawsBackground")  // 角の透過を保持

let snapper = Snapper(out: outPath, size: size)
webView.navigationDelegate = snapper

// SVG を 100% 表示にする最小 HTML でラップ（余白なし）
let svg = try! String(contentsOfFile: svgPath, encoding: .utf8)
let html = """
<!doctype html><html><head><meta charset="utf-8">
<style>html,body{margin:0;padding:0;background:transparent}
svg{display:block;width:\(size)px;height:\(size)px}</style></head>
<body>\(svg)</body></html>
"""
webView.loadHTMLString(html, baseURL: URL(fileURLWithPath: svgPath).deletingLastPathComponent())

// タイムアウト保険
DispatchQueue.main.asyncAfter(deadline: .now() + 15) {
    FileHandle.standardError.write("timeout\n".data(using: .utf8)!); exit(3)
}
app.run()
