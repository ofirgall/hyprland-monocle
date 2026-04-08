# compile with HYPRLAND_HEADERS=<path_to_hl> make all
# make sure that the path above is to the root hl repo directory, NOT src/
# and that you have ran `make protocols` in the hl dir.

.PHONY: all debug clean install

PLUGIN_NAME=monocle
SOURCE_FILES=$(wildcard ./*.cpp)

all:
	g++ -shared -Wall -fPIC $(SOURCE_FILES) -g -DWLR_USE_UNSTABLE `pkg-config --cflags pixman-1 libdrm hyprland pangocairo` -std=c++23 -O2 -o $(PLUGIN_NAME).so

debug:
	g++ -shared -Wall -fPIC $(SOURCE_FILES) -g -DWLR_USE_UNSTABLE `pkg-config --cflags pixman-1 libdrm hyprland pangocairo` -std=c++23 -o $(PLUGIN_NAME).so

clean:
	rm -f ./$(PLUGIN_NAME).so

install:
	hyprpm disable monocle || true
	hyprctl plugin unload $(CURDIR)/$(PLUGIN_NAME).so
	sleep 2
	hyprctl plugin load $(CURDIR)/$(PLUGIN_NAME).so

release:
	hyprctl plugin unload $(CURDIR)/$(PLUGIN_NAME).so # Unload debug plugin

	# Reinstall plugin from git
	git push
	hyprpm purge-cache
	hyprpm update
	hyprpm -v add https://github.com/ofirgall/hyprland-monocle
	hyprpm enable monocle

	hyprpm reload -n # Reload the plugins

unload:
	hyprctl plugin unload $(CURDIR)/$(PLUGIN_NAME).so # Unload debug plugin
	hyprpm disable monocle
