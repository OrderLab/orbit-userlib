CFLAGS := -Wall -Wextra 
CXXFLAGS := -Wall -Wextra -std=c++11 

c_sources			:= $(filter %.c,$(sources))
cxx_sources		:= $(filter %.cc %.cpp,$(sources))

# add OBJDIR prefix in case we'd like to put object files in a different dir
c_objects   	:= $(addprefix $(OBJDIR)/,$(notdir $(c_sources:.c=.o)))
cxx_objects   := $(addprefix $(OBJDIR)/,$(notdir $(filter %.o,$(cxx_sources:.cc=.o) $(cxx_sources:.cpp=.o))))
objects 			:= $(c_objects) $(cxx_objects)

dependencies 	:= $(patsubst %.o,%.d,$(objects))

CFLAGS     		+= $(addprefix -I ,$(include_dirs))
CXXFLAGS     	+= $(addprefix -I ,$(include_dirs))
