#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <shellapi.h>


// Declare the window procedure.
// Windows will call this function when the window receives messages.
LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam
);


int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    PWSTR pCmdLine,
    int nCmdShow)
{
    // =========================================================
    // COM INITIALIZATION
    // =========================================================

    // Initialize COM for the current thread.
    HRESULT comInitialCheck =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Check whether COM initialization failed.
    if (FAILED(comInitialCheck))
    {
        return 0;
    }


    // =========================================================
    // AUDIO DEVICE ENUMERATOR
    // =========================================================

    // Pointer to the COM object used to find audio devices.
    IMMDeviceEnumerator* pEnumerator = nullptr;

    // Create an MMDeviceEnumerator COM object and obtain
    // its IMMDeviceEnumerator interface.
    comInitialCheck = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&pEnumerator)
    );

    if (FAILED(comInitialCheck))
    {
        CoUninitialize();
        return 0;
    }


    // =========================================================
    // GET DEFAULT MICROPHONE
    // =========================================================

    // Pointer that will receive the default capture device.
    IMMDevice* firstMicrophone = nullptr;

    // Find the default audio capture device (microphone).
    comInitialCheck = pEnumerator->GetDefaultAudioEndpoint(
        eCapture,          // Request an input/capture device.
        eConsole,          // Use the default console audio device.
        &firstMicrophone   // Store the resulting device pointer here.
    );

    if (FAILED(comInitialCheck))
    {
        pEnumerator->Release();
        CoUninitialize();
        return 0;
    }


    // =========================================================
    // WINDOW CLASS
    // =========================================================

    // Define the name of our window class.
    const wchar_t CLASS_NAME[] = L"Window Class";

    // Create and zero-initialize a WNDCLASS structure.
    WNDCLASS wc = { };

    // Set the function that will process window messages.
    wc.lpfnWndProc = WindowProc;

    // Associate this window class with this application instance.
    wc.hInstance = hInstance;

    // Set the name of the window class.
    wc.lpszClassName = CLASS_NAME;

    // Register the window class with Windows.
    RegisterClass(&wc);


    // =========================================================
    // CREATE WINDOW
    // =========================================================

    // Create an actual window based on the registered window class.
    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"Learn to Program Windows",
        WS_OVERLAPPEDWINDOW,

        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,

        NULL,
        NULL,
        hInstance,
        NULL
    );

    // Check whether window creation failed.
    if (hwnd == NULL)
    {
        firstMicrophone->Release();
        pEnumerator->Release();
        CoUninitialize();

        return 0;
    }

    // Make the window visible.
    ShowWindow(hwnd, nCmdShow);


    // =========================================================
    // MESSAGE LOOP
    // =========================================================

    MSG msg = { };

    // Keep receiving and processing Windows messages
    // until WM_QUIT is received.
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }


    // =========================================================
    // CLEANUP
    // =========================================================

    // Release the microphone COM interface.
    firstMicrophone->Release();

    // Release the audio device enumerator COM interface.
    pEnumerator->Release();

    // Uninitialize COM for this thread.
    CoUninitialize();

    return 0;
}


// =========================================================
// WINDOW PROCEDURE
// =========================================================

// Windows calls this function when this window receives a message.
LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:

        // Tell the message loop that the application should exit.
        PostQuitMessage(0);

        return 0;


    case WM_PAINT:
    {
        PAINTSTRUCT ps;

        // Begin painting the window.
        HDC hdc = BeginPaint(hwnd, &ps);

        // Fill the window background.
        FillRect(
            hdc,
            &ps.rcPaint,
            (HBRUSH)(COLOR_WINDOW + 1)
        );

        // Finish painting.
        EndPaint(hwnd, &ps);
    }

    return 0;
    }

    // Let Windows handle all messages that we did not process.
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}