# Run from build/app after building the application with make release.
include Makefile.Release
capture-lifecycle.o: ../../tests/capture-lifecycle.cpp
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCPATH) -c $< -o $@
capture-lifecycle: capture-lifecycle.o $(filter-out release/main.o,$(OBJECTS))
	$(LINK) $(LFLAGS) -Wl,--wrap=LiSendKeyboardEvent -Wl,--wrap=LiSendKeyboardEvent2 -o $@ $^ $(LIBS)
