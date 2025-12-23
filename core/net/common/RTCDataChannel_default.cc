// Default (stub) RTCDataChannel implementation
// Used when no platform-specific implementation is available
// Note: RTCDataChannel is typically created by RTCPeerConnection,
// but this provides the common code that doesn't depend on platform.

// This file is intentionally mostly empty as the common implementation
// in RTCDataChannel.cc provides all the shared functionality.
// Platform-specific code creates the actual DataChannel instances.

// When compiled on unsupported platforms, RTCPeerConnection::createDataChannel
// returns nullptr, so no stub DataChannel implementation is needed.
