// Multi-Top © Albert Bregonia 2021
package main

import (
	"embed"
	"fmt"
	"io/fs"
	"log"
	"net/http"
	"sync"

	"github.com/gorilla/websocket"
)

var (
	//go:embed frontend/* server.key server.crt
	embedded   embed.FS
	wsUpgrader = websocket.Upgrader{
		ReadBufferSize:  512,
		WriteBufferSize: 512,
	}
	//displays is a map of IDs for displays that are waiting for a connection
	displays     = make(map[string]*SignalingSocket)
	displayMutex = sync.RWMutex{}
)

//SignalingSocket is a thread safe WebSocket used only for establishing WebRTC connections
type SignalingSocket struct {
	*websocket.Conn
	sync.Mutex
}

//SendSignal is a thread safe wrapper for the `websocket.WriteJSON()` function that only sends the JSON form of a `Signal` struct
func (signaler *SignalingSocket) SendSignal(s Signal) error {
	signaler.Lock()
	defer signaler.Unlock()
	return signaler.WriteJSON(s)
}

//Signals to be written on a SignalingSocket in order to establish WebRTC connections
type Signal struct {
	Event string `json:"event"`
	Data  string `json:"data"`
}

func main() {
	frontend, _ := fs.Sub(embedded, `frontend`)
	http.Handle(`/`, http.FileServer(http.FS(frontend)))
	http.HandleFunc(`/register`, RegisterDisplay)
	http.HandleFunc(`/connect`, ConnectDisplay)
	log.Println(`Server Initialized`)
	log.Fatal(http.ListenAndServeTLS(`:443`, `server.crt`, `server.key`, nil))
}

//RegisterDisplay adds the WebSocket connection for a display to a map of valid connections known as `displays`
func RegisterDisplay(w http.ResponseWriter, r *http.Request) {
	if e := r.ParseForm(); e != nil {
		http.Error(w, e.Error(), http.StatusBadRequest)
		return
	}
	id := r.FormValue(`id`)
	if id == `` {
		http.Error(w, `Display id cannot be empty`, http.StatusBadRequest)
		return
	}
	displayMutex.RLock()
	display := displays[id] //check if display has already been registered
	displayMutex.RUnlock()
	if display != nil {
		http.Error(w, fmt.Sprintf(`Display '%v' has already been registered`, id), http.StatusBadRequest)
		return
	}
	ws, e := wsUpgrader.Upgrade(w, r, nil) //websocket for signaling
	if e != nil {
		http.Error(w, e.Error(), http.StatusBadRequest)
		return
	}
	displayMutex.Lock() //add connection to map of displays
	displays[id] = &SignalingSocket{ws, sync.Mutex{}}
	displays[id].SetCloseHandler(func(_ int, _ string) error {
		displayMutex.Lock() //if the display websocket closes, remove it from pending
		defer displayMutex.Unlock()
		delete(displays, id)
		return nil
	})
	displayMutex.Unlock()
}

//ConnectDisplay uses the WebSocket connection of a registered display to establish a WebRTC connection with a viewer
func ConnectDisplay(w http.ResponseWriter, r *http.Request) {
	if e := r.ParseForm(); e != nil {
		http.Error(w, e.Error(), http.StatusBadRequest)
		return
	}
	id := r.FormValue(`id`)
	if id == `` {
		http.Error(w, `Display id cannot be empty`, http.StatusBadRequest)
		return
	}
	displayMutex.Lock()
	display := displays[id]
	delete(displays, id)
	displayMutex.Unlock()
	if display == nil { //check if valid display id has been given
		http.Error(w, fmt.Sprintf(`Display '%v' has not been registered`, id), http.StatusBadRequest)
		return
	}

	ws, e := wsUpgrader.Upgrade(w, r, nil) //websocket for signaling
	if e != nil {
		http.Error(w, e.Error(), http.StatusBadRequest)
		return
	}
	viewer := SignalingSocket{ws, sync.Mutex{}}

	defer func() {
		display.Close()
		viewer.Close()
	}()
	display.SendSignal(Signal{`offer-request`, `{}`}) //tell the display to send its offer
	go Signaler(display, &viewer)                     //send signals from the display to the viewer
	log.Println(Signaler(&viewer, display))           //send signals from the viewer to the display
}

//Signaler is a function that allows two frontends to communicate with each other using WebSockets
func Signaler(from, to *SignalingSocket) error {
	var signal Signal
	for {
		if e := from.ReadJSON(&signal); e != nil { //receive signals from front end
			return e
		}
		to.SendSignal(signal) //send signal to peer
	}
}
