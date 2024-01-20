This project implements a Video On Demand (VOD) System over UDP with sending and receiving to a multicast IP address. The application can run on the same machine and allows data flow from the server to the client.

## Configuration

- Either client or server can start first.
- Explicit command-line configuration for:
  - Multicast IP address (range 239.0.0.0/8, for local use within a domain).
  - Video file on the server.

## Usage Examples

On equipment 1:

`./vitextserver 1.vtx 239.0.0.1`

On equipment 2:

`./vitextclient 239.0.0.1`

## Initial Scenario
Let's assume the client starts first:

- **P1-P4:** The server waits for 100ms (buffer) and begins playback.
- **P5-P7:** The client starts with a buffer of 100 and waits for the server to send data.

## Server and Client Behavior

- The server behaves as if it were generating real-time content.
  - It sends packets of a frame according to the spacing indicated in the .vtx file.
- The client implements buffering with a selectable value on the command line.

## Handling of Frames and Packets

- A frame can be transmitted in multiple packets.
- A frame is only played if all the packets that compose it have arrived.

## Interpretation of Headers

- The client interprets the RTP header in the packet transmission.
  - Reads RTP header.
  - Reads VTX header.
  - Displays the data on the screen at the appropriate time.

## Client Task Decoupling

It is necessary to decouple the following tasks:

1. Reception of data packets.
2. Waiting to play the next frame.
3. Signal handling (Ctrl-C).
4. Generation and sending of RTCP messages.
5. Reception and processing of RTCP messages.

To achieve this, `select` is used along with a packet buffer.

## packet_buffer

The `packet_buffer` component is specifically developed for `vtx` and:

- Stores relevant information such as sequence number, timestamp, frame size, etc.
- `pbuf_insert`: Fills a packet with part of the RTP header, vtx header, and data.
- `pbuf_retrieve`: Returns a pointer to the data and relevant parameters (RTP, vtx).

## Advanced Scenarios in vitextserver

`vitextserver` allows specifying patterns for:

- Packet loss.
- Packet reordering.
- Time without sending.

`./vitextserver --packet-loss 0.1 --packet-reorder 0.05 --time-without-send 500`
