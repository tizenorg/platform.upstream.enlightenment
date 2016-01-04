%bcond_with x
%bcond_with wayland

Name:           enlightenment
Version:        0.19.99
Release:        0
License:        BSD-2-Clause
Summary:        The Enlightenment window manager
Url:            http://www.enlightenment.org/
Group:          Graphics/EFL
Source0:        enlightenment-%{version}.tar.bz2
Source1001:     enlightenment.manifest

%if "%{profile}" != "common"
%define light_e 1
%define _unpackaged_files_terminate_build 0
%endif

BuildRequires:  doxygen
BuildRequires:  eet-tools
BuildRequires:  fdupes
BuildRequires:  gettext
BuildRequires:  pam-devel
BuildRequires:  pkgconfig(alsa)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(ecore-con)
BuildRequires:  pkgconfig(ecore-evas)
BuildRequires:  pkgconfig(ecore-file)
BuildRequires:  pkgconfig(ecore-input)
BuildRequires:  pkgconfig(ecore-input-evas)
BuildRequires:  pkgconfig(ecore-ipc)
BuildRequires:  pkgconfig(edbus)
BuildRequires:  pkgconfig(edje)
BuildRequires:  pkgconfig(eet)
BuildRequires:  pkgconfig(eeze)
BuildRequires:  pkgconfig(efreet)
BuildRequires:  pkgconfig(eina)
BuildRequires:  pkgconfig(eio)
BuildRequires:  pkgconfig(evas)
BuildRequires:  pkgconfig(elementary)
BuildRequires:  pkgconfig(ice)
BuildRequires:  pkgconfig(libudev)
BuildRequires:  pkgconfig(udev)
BuildRequires:  pkgconfig(libtbm)
%if %{with x}
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(xcb-keysyms)
BuildRequires:  pkgconfig(ecore-x)
%else
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(xdg-shell-server)
BuildRequires:  pkgconfig(scaler-server)
BuildRequires:  pkgconfig(transform-server)
BuildRequires:  pkgconfig(screenshooter-server)
BuildRequires:  pkgconfig(screenshooter-client)
BuildRequires:  pkgconfig(tizen-extension-server)
BuildRequires:  pkgconfig(wayland-tbm-server)
BuildRequires:  pkgconfig(ecore-drm)
Requires:       libwayland-extension-server
%endif


%description
Enlightenment is a window manager.

%package devel
Summary:        Development components for the enlightenment package
Group:          Development/Libraries
Requires:       %{name} = %{version}

%description devel
Development files for enlightenment

%prep
%setup -q -n %{name}-%{version}
cp %{SOURCE1001} .


%build
export CFLAGS+=" -fPIE "
export LDFLAGS+=" -pie "
%autogen \
%if %{with wayland}
      --enable-wayland \
      --enable-wl-drm \
      --disable-shot \
      --disable-xkbswitch \
      --disable-conf-randr \
      --disable-conf-bindings \
      --disable-conf-display \
      --disable-conf-theme \
      --disable-everything \
      --disable-fileman \
      --disable-pager \
      --disable-pager-plain \
      --disable-systray \
      --disable-tiling \
      --disable-winlist \
      --disable-wizard \
      --disable-wl-x11 \
      --enable-quick-init \
%endif
%if 0%{?light_e}
      --enable-light-e \
      --disable-appmenu \
      --disable-backlight \
      --disable-battery \
      --disable-bluez4 \
      --disable-clock \
      --disable-conf \
      --disable-conf_applications \
      --disable-conf_dialogs \
      --disable-conf_interaction \
      --disable-conf_intl \
      --disable-conf_menus \
      --disable-conf_paths \
      --disable-conf_performance \
      --disable-conf_shelves \
      --disable-conf_window_manipulation \
      --disable-conf_window_remembers \
      --disable-connman \
      --disable-contact  \
      --disable-cpufreq \
      --disable-fileman_opinfo \
      --disable-gadman \
      --disable-ibar \
      --disable-ibox \
      --disable-lokker \
      --disable-mixer \
      --disable-msgbus \
      --disable-music_control \
      --disable-notification \
      --disable-packagekit \
      --disable-policy_mobile \
      --disable-quickaccess \
      --disable-start \
      --disable-syscon \
      --disable-tasks \
      --disable-teamwork \
      --disable-temperature \
%endif
      --enable-mount-eeze

make %{?_smp_mflags}

%install
%make_install

mkdir -p %{buildroot}%{_sysconfdir}/dbus-1/system.d
rm -f %{buildroot}%{_prefix}/lib/systemd/user/enlightenment.service

%find_lang enlightenment
%fdupes  %{buildroot}/%{_libdir}/enlightenment
%fdupes  %{buildroot}/%{_datadir}/enlightenment

%lang_package

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%license COPYING
%config %{_sysconfdir}/enlightenment/sysactions.conf
%{_bindir}/enlightenment*
%{_libdir}/enlightenment/*
%{_datadir}/enlightenment/*
%{_datadir}/xsessions/enlightenment.desktop
%{_sysconfdir}/xdg/menus/e-applications.menu
%{_datadir}/applications/enlightenment_filemanager.desktop
%{_sysconfdir}/dbus-1/system.d/org.enlightenment.wm.conf
%exclude /usr/share/enlightenment/data/config/profile.cfg
%if 0%{?light_e}
%exclude %{_bindir}/enlightenment_filemanager
%exclude %{_bindir}/enlightenment_imc
%exclude %{_bindir}/enlightenment_open
%exclude %{_bindir}/enlightenment_remote
%exclude %{_bindir}/enlightenment_start
%exclude %{_libdir}/enlightenment/utils/*
%exclude %{_libdir}/enlightenment/utils/
%exclude %{_datadir}/enlightenment/data/*
%exclude %{_datadir}/enlightenment/data/
%exclude %{_datadir}/enlightenment/doc/*
%exclude %{_datadir}/enlightenment/doc
%exclude %{_datadir}/xsessions/enlightenment.desktop
%exclude %{_sysconfdir}/xdg/menus/e-applications.menu
%exclude %{_datadir}/applications/enlightenment_filemanager.desktop
%endif

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_includedir}/enlightenment/*
%{_libdir}/pkgconfig/*.pc
