-include $(dependencies)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP $< -o $@ -c

$(OBJDIR)/%.o: $(SRCDIR)/%.cc | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP $< -o $@ -c

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP $< -o $@ -c

$(sort $(BINDIR) $(OBJDIR)):
	mkdir -p $@

clean:
	-rm -f $(objects) $(dependencies) $(bins)
	@if [ -d $(OBJDIR) ]; then rmdir $(OBJDIR); fi 
	@if [ -d $(BINDIR) ]; then rmdir $(BINDIR); fi 
