package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"log"
	"os"

	"go.bug.st/serial"
)

// Gets a line from the serial port, not including the newline
func getLine(port serial.Port) (string, error) {
	var line string

	buff := make([]byte, 1)
	for {
		// Read a byte. Not very efficent...
		n, err := port.Read(buff)
		if err != nil {
			return "", err
		}
		if n == 0 {
			fmt.Println("\nEOF")
			break
		}

		// If we receive a newline stop reading
		if buff[0] == '\n' {
			break
		}

		line += string(buff)
	}

	return line, nil
}

// Read a file into a []byte, padding it to the page size
func readFileWithPadding(filename string) ([]byte, error) {
	fileData, err := os.ReadFile(filename)
	if err != nil {
		return nil, err
	}

	for (len(fileData) % 256) != 0 {
		fileData = append(fileData, byte(0))
	}

	return fileData, nil
}

func reprogramFlash(port serial.Port, fileData []byte) error {
	// Get the +++ prompt
	_, err := getLine(port)
	if err != nil {
		return fmt.Errorf("could not read line from port: %s", err)
	}

	// Get the size of the file in 256 byte pages and send it, little endian
	fileDataLengthAsBytes := make([]byte, 4)
	binary.LittleEndian.PutUint32(fileDataLengthAsBytes, uint32(len(fileData)/256))
	_, err = port.Write(fileDataLengthAsBytes)
	if err != nil {
		return fmt.Errorf("could not write to port: %s", err)
	}

	for startAddress := 0; startAddress < len(fileData); startAddress += 256 {
		// Write this page
		_, err = port.Write(fileData[startAddress : startAddress+256])
		if err != nil {
			return fmt.Errorf("could not write to port: %s", err)
		}

		// Read the end-page pause "wakeup", which indicates the programmer has written this page and
		// the client should send the next
		buff := make([]byte, 1)
		_, err = port.Read(buff)
		if err != nil {
			return fmt.Errorf("could not read mark from port: %s", err)
		}

		fmt.Printf("#")
	}

	fmt.Printf("\nVerifiying...\n")

	var validationFailedCount = 0
	for address := 0; address < len(fileData); address += 256 {
		// Read the page via repeated port.Read() calls then check it against what we sent
		readbackPage := make([]byte, 256)
		for c := 0; c < 256; {
			n, err := port.Read(readbackPage[c:256])
			if err != nil {
				return fmt.Errorf("could not read back from port: %s", err)
			}
			c += n
		}
		if !bytes.Equal(fileData[address:address+256], readbackPage) {
			fmt.Printf("Expected: %+v\n", fileData[address:address+256])
			fmt.Printf("Got: %+v\n", readbackPage)

			// This will continue on, no retries etc
			fmt.Printf("Bad data\n")

			validationFailedCount++
		}

		fmt.Printf("#")
	}

	fmt.Printf("\nDone\n")

	if validationFailedCount > 0 {
		return fmt.Errorf("validation failed on %d pages", validationFailedCount)
	} else {
		return nil
	}
}

func readFlash(port serial.Port, capacity int) ([]byte, error) {
	fmt.Printf("Reading...\n")

	fileData := make([]byte, capacity)

	for address := 0; address < capacity; address += 256 {
		// Read the page via repeated port.Read() calls then check it against what we sent
		for c := 0; c < 256; {
			n, err := port.Read(fileData[address+c : address+256])
			if err != nil {
				return nil, fmt.Errorf("could not read back from port: %s", err)
			}
			c += n
		}
		fmt.Printf("#")
	}

	fmt.Printf("\nDone\n")

	return fileData, nil
}

func main() {
	writeMode := flag.Bool("write", false, "Enable writing mode")
	readMode := flag.Bool("read", false, "Enable reading mode")
	portPtr := flag.String("port", "/dev/ttyACM0", "device node")
	filenamePtr := flag.String("filename", "", ".bin file to flash")

	flag.Parse()

	if *portPtr == "" {
		log.Fatal("Port not set")
	}
	if *filenamePtr == "" {
		log.Fatal("Filename not set")
	}
	if *readMode && *writeMode {
		log.Fatal("Cannot specify both read and write modes")
	}
	if !*readMode && !*writeMode {
		log.Fatal("Must specify either read or write mode")
	}

	// No mode content, since we only really support USB TTYs
	port, err := serial.Open(*portPtr, &serial.Mode{})
	if err != nil {
		log.Fatal("Power on Pico and try again")
	}

	// Wake up the programmer
	_, err = port.Write([]byte(" "))
	if err != nil {
		log.Fatal("Could not write to port")
	}

	// Get the device type
	banner, err := getLine(port)
	if err != nil {
		log.Fatalf("Could not read line from port: %s", err)
	}

	// Get the banner line and parse it into a device string and a capacity
	var name string
	var capacity_bytes int
	_, err = fmt.Sscanf(banner, "%s %d", &name, &capacity_bytes)
	if err != nil {
		log.Fatalf("Could not parse the banner, got [%s]: %s", banner, err)
	}
	fmt.Printf("Device: %s Capacity: %d\n", name, capacity_bytes)

	if *writeMode {
		// Send the "we want to flash" code
		_, err = port.Write([]byte("f"))
		if err != nil {
			log.Fatalf("Could not send flash command: %s", err)
		}

		fileData, err := readFileWithPadding(*filenamePtr)
		if err != nil {
			log.Fatalf("Could not read filename %s", *filenamePtr)
		}
		fmt.Printf("File is %d bytes (%d pages)\n", len(fileData), len(fileData)/256)

		reprogramFlash(port, fileData)
	} else if *readMode {
		// Send the "we want to flash" code
		_, err = port.Write([]byte("r"))
		if err != nil {
			log.Fatalf("Could not send flash command: %s", err)
		}

		fileData, err := readFlash(port, capacity_bytes)
		if err != nil {
			log.Fatalf("Could not read flash: %s", err)
		}

		err = os.WriteFile(*filenamePtr, fileData, 0664)
		if err != nil {
			log.Fatalf("Could not write file: %s", err)
		}
	}
}
