# Run from build/app after building the application with make release.
include Makefile.Release
capture-lifecycle.o: ../../tests/capture-lifecycle.cpp
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCPATH) -c $< -o $@
capture-lifecycle: capture-lifecycle.o $(filter-out release/main.o,$(OBJECTS))
	$(LINK) $(LFLAGS) -Wl,--wrap=LiSendKeyboardEvent -Wl,--wrap=LiSendKeyboardEvent2 -Wl,--wrap=LiSendMouseButtonEvent -o $@ $^ $(LIBS)
clipboard-paste.o: ../../tests/clipboard-paste.cpp
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCPATH) -c $< -o $@
clipboard-paste: clipboard-paste.o $(filter-out release/main.o,$(OBJECTS))
	$(LINK) $(LFLAGS) -Wl,--wrap=LiSendKeyboardEvent -Wl,--wrap=LiSendKeyboardEvent2 -Wl,--wrap=LiSendUtf8TextEvent -o $@ $^ $(LIBS)
