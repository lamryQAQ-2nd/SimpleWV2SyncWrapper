# WebView2 Event Synchronization Wrapper for Win32 C++

## Overview

This project provides a simple wrapper around WebView2's event-handling mechanism in Win32 C++. It allows for synchronous waiting of WebView events through the usage of custom macros and framework utilities. This can be especially useful for applications where event-based programming is not preferred, or where it's necessary to halt execution until a particular WebView2 event is triggered.

## Features

- **Synchronous Event Handling**: Wait for WebView2 events to complete synchronously.
- **Easy to Use Macros**: Use `DEFINE_WEBVIEW_EVENT` and `DEFINE_WEBVIEW_EVENT_WITH_ARGS` macros for easy event handling setup.
- **Compatible with COM**: Utilizes Microsoft's Component Object Model (COM) for robustness and interoperability.

## Getting Started

To get started, include the provided header file in your project.

### Define Event Handlers

Use `DEFINE_WEBVIEW_EVENT` to define an event. Here's an example for handling the `NavigationCompleted` event.

```cpp
DEFINE_WEBVIEW_EVENT(NavigationCompleted, ICoreWebView2);
```

### Wait for Events Synchronously

Use `WaitEvent` to wait for an event to be triggered. Here's an example:

```cpp
Microsoft::WRL::ComPtr<ICoreWebView2> webview = create_webview();
WaitEvent<NavigationCompleted>(webview);  // This will block until navigation is complete
```

### Access Event Arguments

You can also access event arguments after waiting for an event.

```cpp
DEFINE_WEBVIEW_EVENT(WebMessageReceived, ICoreWebView2);
auto args = WaitEvent<WebMessageReceived>(webview);
PWSTR message = NULL;
HRESULT hr = args->TryGetWebMessageAsString(&message);
```

### Custom Arguments

If the event arguments do not conform to the standard WebView2 event argument interfaces, you can use `DEFINE_WEBVIEW_EVENT_WITH_ARGS`:

```cpp
DEFINE_WEBVIEW_EVENT_WITH_ARGS(DocumentTitleChanged, ICoreWebView2, IUnknown);
```

Then you can call `WaitEvent` as you would for a standard event.

```cpp
WaitEvent<DocumentTitleChanged>(webview);
```
