package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"time"

	"github.com/gorilla/mux"
)

type SensorData struct {
	Sensor      string  `json:"sensor"`
	Temperature float64 `json:"temperature,omitempty"`
	Humidity    float64 `json:"humidity,omitempty"`
	Luminosity  uint16  `json:"luminosity,omitempty"`
}

func dataHandler(w http.ResponseWriter, r *http.Request) {
	var data []SensorData

	decoder := json.NewDecoder(r.Body)
	if err := decoder.Decode(&data); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	defer r.Body.Close()

	for _, entry := range data {
		log.Printf("Received data from %s: %v at %s", entry.Sensor, entry, time.Now().Format(time.RFC3339))
	}

	w.WriteHeader(http.StatusOK)
	w.Write([]byte("Data received successfully"))
}

func main() {
	router := mux.NewRouter()
	router.HandleFunc("/data", dataHandler).Methods("POST")

	fmt.Println("Starting server on :8080")
	log.Fatal(http.ListenAndServe(":8080", router))
}
