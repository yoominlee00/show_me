package main

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"math/rand/v2"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"
)

type event struct {
	AgentID    string         `json:"agentId"`
	EventType  string         `json:"eventType"`
	OccurredAt time.Time      `json:"occurredAt"`
	Payload    map[string]any `json:"payload"`
}

func main() {
	endpoint := flag.String("endpoint", env("INGEST_URL", "http://localhost:8080/api/v1/events"), "ingestion endpoint")
	token := flag.String("token", env("INGEST_API_TOKEN", "local-dev-token"), "API token")
	agents := flag.Int("agents", 3, "number of simulated agents")
	interval := flag.Duration("interval", time.Second, "interval per agent")
	flag.Parse()

	if *agents < 1 || *interval <= 0 {
		log.Fatal("agents must be >= 1 and interval must be positive")
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	client := &http.Client{Timeout: 5 * time.Second}
	var wg sync.WaitGroup
	for i := 1; i <= *agents; i++ {
		wg.Add(1)
		go runAgent(ctx, &wg, client, *endpoint, *token, fmt.Sprintf("sim-agent-%03d", i), *interval)
	}
	<-ctx.Done()
	wg.Wait()
}

func runAgent(ctx context.Context, wg *sync.WaitGroup, client *http.Client, endpoint, token, agentID string, interval time.Duration) {
	defer wg.Done()
	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	for {
		send(client, endpoint, token, agentID)
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
		}
	}
}

func send(client *http.Client, endpoint, token, agentID string) {
	e := event{
		AgentID: agentID, EventType: "host.metrics", OccurredAt: time.Now().UTC(),
		Payload: map[string]any{
			"cpuUsagePercent": 20 + rand.Float64()*70,
			"memoryUsagePercent": 30 + rand.Float64()*55,
			"loadAverage1m": rand.Float64() * 8,
		},
	}
	body, err := json.Marshal(e)
	if err != nil { log.Printf("encode event: %v", err); return }
	req, err := http.NewRequest(http.MethodPost, endpoint, bytes.NewReader(body))
	if err != nil { log.Printf("create request: %v", err); return }
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-API-Token", token)
	response, err := client.Do(req)
	if err != nil { log.Printf("agent=%s send failed: %v", agentID, err); return }
	defer response.Body.Close()
	if response.StatusCode != http.StatusAccepted { log.Printf("agent=%s unexpected status=%d", agentID, response.StatusCode) }
}

func env(key, fallback string) string {
	if value := os.Getenv(key); value != "" { return value }
	return fallback
}

