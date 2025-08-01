#include "EZProgram.h"

LRESULT CALLBACK EZ::Program::CustomWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	EZ::Program* program = reinterpret_cast<EZ::Program*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

	if (program == NULL) {
		// If the window is still initializing GWLP_USERDATA will be null.
		// In this case just call DefWindowProc and keep waiting for the window to initialize.
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	if (uMsg == WM_CLOSE && program->_programSettings.IgnoreWMClose) {
		return 0; // We do not care
	}
	if (uMsg == WM_SIZE && !program->_programSettings.DontResizeBuffer) {
		UINT32 newWidth = static_cast<UINT32>(LOWORD(lParam));
		UINT32 newHeight = static_cast<UINT32>(HIWORD(lParam));
		program->_newSize = D2D1::SizeU(newWidth, newHeight);
		program->_resizeRequested = TRUE;
		while (program->_resizeRequested) {}
	}

	if (program->_programSettings.WndProcCallback != NULL && program->_state == EZ::Program::State::Running) {
		return program->_programSettings.WndProcCallback(program, hwnd, uMsg, wParam, lParam);
	}
	else {
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
}

EZ::Program::Program(EZ::ProgramSettings programSettings, EZ::ClassSettings classSettings, EZ::WindowSettings windowSettings, EZ::RendererSettings rendererSettings) {
	if (classSettings.WndProc != NULL) {
		throw Error(L"classSettings.WndProc must be NULL. Use programSettings.WndProcCallback instead.", __FILE__, __LINE__);
	}
	if (classSettings.ThisThreadOnly != TRUE) {
		throw Error(L"classSettings.ThisThreadOnly must be TRUE.", __FILE__, __LINE__);
	}
	if (!windowSettings.LaunchHidden) {
		throw Error(L"windowSettings.LaunchHidden must be TRUE.", __FILE__, __LINE__);
	}
	if (lstrcmp(windowSettings.ClassName, classSettings.Name) != 0) {
		throw Error(L"windowSettings.ClassName and classSettings.Name must match.", __FILE__, __LINE__);
	}
	classSettings.WndProc = CustomWndProc;

	_state = EZ::Program::State::Created;
	_newSize = D2D1::SizeU(0, 0);
	_resizeRequested = FALSE;

	_profiler = NULL;
	_renderer = NULL;
	_window = NULL;

	_programSettings = programSettings;
	_classSettings = classSettings;
	_windowSettings = windowSettings;
	_rendererSettings = rendererSettings;

	if (!_programSettings.DontLogPreformace) {
		_profiler = new EZ::Profiler(_programSettings.PreformanceLogInterval);
	}

	std::thread windowThread([this, classSettings, windowSettings]() {
		try {
			EZ::ClassSettings classSettingsCopy = classSettings;
			EZ::WindowSettings windowSettingsCopy = windowSettings;

			WCHAR* generatedClassName = NULL;
			if (classSettingsCopy.Name == NULL) {
				GUID guid;
				EZ::Error::ThrowFromHR(CoCreateGuid(&guid), __FILE__, __LINE__);

				WCHAR guidString[39];
				if (StringFromGUID2(guid, guidString, 39) < 0) {
					EZ::Error::ThrowFromCode(GetLastError(), __FILE__, __LINE__);
				}

				generatedClassName = new WCHAR[57];

				lstrcpy(generatedClassName, L"EZProgramAutoClass");
				lstrcpy(&generatedClassName[18], guidString);

				classSettingsCopy.Name = generatedClassName;
				windowSettingsCopy.ClassName = generatedClassName;
			}

			EZ::RegisterClass(&classSettingsCopy);

			_window = new EZ::Window(&windowSettingsCopy);

			if (generatedClassName != NULL) {
				delete[] generatedClassName;
			}

			SetLastError(0);
			SetWindowLongPtr(_window->GetHandle(), GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
			if (GetLastError() != 0) {
				EZ::Error::ThrowFromCode(GetLastError(), __FILE__, __LINE__);
			}

			while (_state != EZ::Program::State::Running) {}

			_window->Show();
			_window->Run();

			_state = EZ::Program::State::Closed;

			while (_state != EZ::Program::State::Destroyed) {}

			delete _window;
		}
		catch (EZ::Error error) { error.Print(); std::exit(1); }
		});
	windowThread.detach();

	while (_window == NULL) {}

	_renderer = new EZ::Renderer(_window->GetHandle(), &rendererSettings);

	if (!_programSettings.DontResizeBuffer) {
		RECT windowSize = {};
		if (!GetWindowRect(_window->GetHandle(), &windowSize)) {
			EZ::Error::ThrowFromCode(GetLastError(), __FILE__, __LINE__);
		}
		D2D1_SIZE_U newSize = D2D1::SizeU(windowSize.right - windowSize.left, windowSize.bottom - windowSize.top);
		_renderer->Resize(newSize);
	}
}
void EZ::Program::Run() {
	if (_state != EZ::Program::State::Created) {
		throw Error(L"Program can only be ran once.");
	}

	_state = EZ::Program::State::Running;

	while (_state == EZ::Program::State::Running) {
		if (_resizeRequested) {
			_renderer->Resize(_newSize);
			_resizeRequested = FALSE;
		}

		_renderer->BeginDraw();
		if (_programSettings.UpdateCallback != NULL) {
			_programSettings.UpdateCallback(this);
		}
		_renderer->EndDraw();

		if (!_programSettings.DontLogPreformace) {
			_profiler->Tick();
		}
	}
}
EZ::Program::~Program() {
	_state = EZ::Program::State::Destroyed;

	delete _renderer;
	if (!_programSettings.DontLogPreformace) {
		delete _profiler;
	}
}

EZ::Renderer* EZ::Program::GetRenderer() const {
	return _renderer;
}
EZ::Window* EZ::Program::GetWindow() const {
	return _window;
}
EZ::ProgramSettings EZ::Program::GetProgramSettings() const {
	return _programSettings;
}
EZ::ClassSettings EZ::Program::GetClassSettings() const {
	return _classSettings;
}
EZ::WindowSettings EZ::Program::GetWindowSettings() const {
	return _windowSettings;
}
EZ::RendererSettings EZ::Program::GetRendererSettings() const {
	return _rendererSettings;
}