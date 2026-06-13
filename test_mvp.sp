class SystemMonitor {
    var lastCheck = 0
    readonly set OS = "Unix"

    define init = () => {
        this.lastCheck = time()
    }

    define checkDisk = () => {
        set info = process.run("df", ["-h", "/"])
        console.show("System: {this.OS} | Status: {info.status}")
        this.lastCheck = time()
    }
}

set monitor = SystemMonitor()
monitor.checkDisk()
console.show("Last update at: {monitor.lastCheck}")