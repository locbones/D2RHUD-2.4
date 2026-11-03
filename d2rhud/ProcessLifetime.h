#pragma once
// Native driver objects must not be released from CRT/loader-lock teardown.
// Explicit runtime reset still releases the contents normally. At process exit
// the operating system reclaims this one state allocation without a destructor.
namespace HudLifetime {
template<class T>T& State(){static T* const state=new T;return *state;}
}
