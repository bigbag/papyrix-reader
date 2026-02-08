package main

import (
	"testing"
)

func TestFilterPorts_Linux(t *testing.T) {
	ports := []string{"/dev/ttyACM0", "/dev/ttyUSB0", "/dev/ttyACM1", "/dev/ttyS0"}
	got := filterPorts(ports, "linux")
	want := []string{"/dev/ttyACM0", "/dev/ttyACM1"}
	assertSliceEqual(t, got, want)
}

func TestFilterPorts_Darwin(t *testing.T) {
	ports := []string{"/dev/cu.usbmodem1101", "/dev/cu.Bluetooth-Incoming-Port", "/dev/cu.usbmodem1201"}
	got := filterPorts(ports, "darwin")
	want := []string{"/dev/cu.usbmodem1101", "/dev/cu.usbmodem1201"}
	assertSliceEqual(t, got, want)
}

func TestFilterPorts_Windows(t *testing.T) {
	ports := []string{"COM1", "COM3", "COM7"}
	got := filterPorts(ports, "windows")
	// All COM ports are candidates on Windows
	assertSliceEqual(t, got, ports)
}

func TestFilterPorts_NoMatch(t *testing.T) {
	ports := []string{"/dev/ttyUSB0", "/dev/ttyS0"}
	got := filterPorts(ports, "linux")
	if got != nil {
		t.Errorf("expected nil, got %v", got)
	}
}

func TestFilterPorts_UnknownOS(t *testing.T) {
	ports := []string{"/dev/ttyACM0", "COM3"}
	got := filterPorts(ports, "freebsd")
	if got != nil {
		t.Errorf("expected nil for unknown OS, got %v", got)
	}
}

func TestFilterPorts_Empty(t *testing.T) {
	got := filterPorts(nil, "linux")
	if got != nil {
		t.Errorf("expected nil for empty input, got %v", got)
	}
}

func TestSelectPort_Single(t *testing.T) {
	port, err := selectPort([]string{"/dev/ttyACM0"}, nil)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if port != "/dev/ttyACM0" {
		t.Errorf("expected /dev/ttyACM0, got %s", port)
	}
}

func TestSelectPort_None(t *testing.T) {
	_, err := selectPort(nil, []string{"/dev/ttyUSB0"})
	if err == nil {
		t.Fatal("expected error for no candidates")
	}
}

func TestSelectPort_Multiple(t *testing.T) {
	_, err := selectPort([]string{"/dev/ttyACM0", "/dev/ttyACM1"}, nil)
	if err == nil {
		t.Fatal("expected error for multiple candidates")
	}
}

func assertSliceEqual(t *testing.T, got, want []string) {
	t.Helper()
	if len(got) != len(want) {
		t.Fatalf("length mismatch: got %v, want %v", got, want)
	}
	for i := range got {
		if got[i] != want[i] {
			t.Errorf("index %d: got %q, want %q", i, got[i], want[i])
		}
	}
}
