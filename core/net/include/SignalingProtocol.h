// Signaling protocol message types and JSON serialization
// Wire-compatible with existing JS signaling-client.js

#ifndef CELLS_NET_SIGNALING_PROTOCOL_H
#define CELLS_NET_SIGNALING_PROTOCOL_H

#include <string>
#include <vector>

#include "core/net/include/RTCPeerConnection.h"

namespace cells::net {

// Message types for signaling protocol
// Outbound (client -> server):
//   join, leave, offer, answer, ice-candidate
// Inbound (server -> client):
//   joined, peer-joined, peer-left, offer, answer, ice-candidate, peer-list, error
namespace SignalingMessageType {
constexpr const char* JOIN = "join";
constexpr const char* LEAVE = "leave";
constexpr const char* OFFER = "offer";
constexpr const char* ANSWER = "answer";
constexpr const char* ICE_CANDIDATE = "ice-candidate";
constexpr const char* JOINED = "joined";
constexpr const char* PEER_JOINED = "peer-joined";
constexpr const char* PEER_LEFT = "peer-left";
constexpr const char* PEER_LIST = "peer-list";
constexpr const char* ERROR = "error";
}  // namespace SignalingMessageType

// JSON builder for outbound messages
// Simple string concatenation - avoids JSON library dependency
namespace SignalingProtocol {

// Build join message: {"type":"join","room":"...","peer_id":"..."}
std::string buildJoinMessage(const std::string& room_id, const std::string& peer_id);

// Build leave message: {"type":"leave","room":"...","peer_id":"..."}
std::string buildLeaveMessage(const std::string& room_id, const std::string& peer_id);

// Build offer message: {"type":"offer","target":"...","sdp":{"type":"offer","sdp":"..."}}
std::string buildOfferMessage(const std::string& target_peer, const SessionDescription& sdp);

// Build answer message: {"type":"answer","target":"...","sdp":{"type":"answer","sdp":"..."}}
std::string buildAnswerMessage(const std::string& target_peer, const SessionDescription& sdp);

// Build ICE candidate message:
// {"type":"ice-candidate","target":"...","candidate":{"candidate":"...","sdpMid":"...","sdpMLineIndex":...}}
std::string buildICECandidateMessage(const std::string& target_peer, const ICECandidate& candidate);

// Parse incoming message type (returns empty string if invalid)
std::string parseMessageType(const std::string& json);

// Parse "joined" message - returns room and list of existing peers
bool parseJoinedMessage(const std::string& json, std::string& room,
                        std::vector<std::string>& peers);

// Parse "peer-joined" message - returns peer_id
bool parsePeerJoinedMessage(const std::string& json, std::string& peer_id);

// Parse "peer-left" message - returns peer_id
bool parsePeerLeftMessage(const std::string& json, std::string& peer_id);

// Parse "offer" message - returns from_peer and sdp
bool parseOfferMessage(const std::string& json, std::string& from_peer, SessionDescription& sdp);

// Parse "answer" message - returns from_peer and sdp
bool parseAnswerMessage(const std::string& json, std::string& from_peer, SessionDescription& sdp);

// Parse "ice-candidate" message - returns from_peer and candidate
bool parseICECandidateMessage(const std::string& json, std::string& from_peer,
                              ICECandidate& candidate);

// Parse "peer-list" message - returns list of peers
bool parsePeerListMessage(const std::string& json, std::vector<std::string>& peers);

// Parse "error" message - returns error message
bool parseErrorMessage(const std::string& json, std::string& error_message);

// Escape a string for JSON (handles quotes, backslashes, control chars)
std::string escapeJsonString(const std::string& input);

// Unescape a JSON string (reverses escapeJsonString)
std::string unescapeJsonString(const std::string& input);

}  // namespace SignalingProtocol

}  // namespace cells::net

#endif  // CELLS_NET_SIGNALING_PROTOCOL_H
