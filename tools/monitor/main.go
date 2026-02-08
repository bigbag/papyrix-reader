package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"runtime"
	"strings"

	"go.bug.st/serial"
)

// filterPorts returns port names matching known ESP32 CDC patterns for the given OS.
func filterPorts(ports []string, goos string) []string {
	var candidates []string
	for _, p := range ports {
		switch goos {
		case "linux":
			if strings.HasPrefix(p, "/dev/ttyACM") {
				candidates = append(candidates, p)
			}
		case "darwin":
			if strings.HasPrefix(p, "/dev/cu.usbmodem") {
				candidates = append(candidates, p)
			}
		case "windows":
			if strings.HasPrefix(p, "COM") {
				candidates = append(candidates, p)
			}
		}
	}
	return candidates
}

// selectPort picks a single port from candidates. Returns an error if zero or multiple found.
func selectPort(candidates []string, allPorts []string) (string, error) {
	switch len(candidates) {
	case 0:
		return "", fmt.Errorf("no serial ports found (available: %v)", allPorts)
	case 1:
		return candidates[0], nil
	default:
		return "", fmt.Errorf("multiple ports found, specify one with -port: %v", candidates)
	}
}

func autoDetectPort() (string, error) {
	ports, err := serial.GetPortsList()
	if err != nil {
		return "", fmt.Errorf("failed to list serial ports: %w", err)
	}
	candidates := filterPorts(ports, runtime.GOOS)
	return selectPort(candidates, ports)
}

func main() {
	portFlag := flag.String("port", "", "serial port (e.g. /dev/ttyACM0, COM3). Auto-detect if omitted")
	speedFlag := flag.Int("speed", 115200, "baud rate")
	logFlag := flag.String("log", "", "log file path (output to both stdout and file)")
	flag.Parse()

	portName := *portFlag
	if portName == "" {
		detected, err := autoDetectPort()
		if err != nil {
			fmt.Fprintf(os.Stderr, "Auto-detect failed: %v\n", err)
			os.Exit(1)
		}
		portName = detected
		fmt.Fprintf(os.Stderr, "Auto-detected port: %s\n", portName)
	}

	mode := &serial.Mode{
		BaudRate: *speedFlag,
		DataBits: 8,
		Parity:   serial.NoParity,
		StopBits: serial.OneStopBit,
	}

	port, err := serial.Open(portName, mode)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to open %s: %v\n", portName, err)
		os.Exit(1)
	}
	defer port.Close()

	fmt.Fprintf(os.Stderr, "Monitoring %s at %d baud. Press Ctrl+C to exit.\n", portName, *speedFlag)

	var out io.Writer = os.Stdout
	if *logFlag != "" {
		f, err := os.OpenFile(*logFlag, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Failed to open log file: %v\n", err)
			os.Exit(1)
		}
		defer f.Close()
		out = io.MultiWriter(os.Stdout, f)
		fmt.Fprintf(os.Stderr, "Logging to %s\n", *logFlag)
	}

	// Handle Ctrl+C
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt)
	go func() {
		<-sig
		fmt.Fprintf(os.Stderr, "\nExiting.\n")
		port.Close()
		os.Exit(0)
	}()

	scanner := bufio.NewScanner(port)
	for scanner.Scan() {
		fmt.Fprintln(out, scanner.Text())
	}
	if err := scanner.Err(); err != nil {
		fmt.Fprintf(os.Stderr, "Read error: %v\n", err)
	}
}
