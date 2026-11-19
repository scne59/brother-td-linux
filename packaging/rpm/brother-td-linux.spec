%global debug_package %{nil}

Name:           printer-driver-brother-td
Version:        1.0.0
Release:        1%{?dist}
Summary:        CUPS driver for Brother TD-4D label printers
License:        MIT
URL:            https://github.com/scne59/brother-td-linux
Source0:        brother-td-linux-%{version}.tar.gz
BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  cups-devel
BuildRequires:  pappl-devel
Requires:       cups
Requires:       cups-filters
Requires:       ghostscript

%description
A native CUPS raster filter and PPDs for Brother TD-4D series direct
thermal label printers.  It supports die-cut labels and continuous rolls,
203 dpi and 300 dpi models, several dithering algorithms,
scaling/rotation, copies, and optional auto-cut/peeler control.

%package -n brother-td-printer-app
Summary:        PAPPL/IPP Everywhere printer application for Brother TD-4D
Requires:       cups
Requires:       cups-filters
Requires:       ghostscript

%description -n brother-td-printer-app
A native PAPPL printer application for Brother TD-4D series direct thermal
label printers.  It exposes the printer as an IPP Everywhere/AirPrint
printer for CUPS 3 and driverless clients, accepting PWG/Apple raster
directly and using Ghostscript for PDF and PostScript jobs.

%prep
%setup -q -n brother-td-linux-%{version}

%build
make %{?_smp_mflags} all-pappl PREFIX=/usr

%install
make install DESTDIR=%{buildroot} PREFIX=/usr \
    DOCDIR=/usr/share/doc/printer-driver-brother-td
rm -rf %{buildroot}/usr/share/doc/printer-driver-brother-td

%post -n brother-td-printer-app
systemctl daemon-reload >/dev/null 2>&1 || :

%preun -n brother-td-printer-app
systemctl daemon-reload >/dev/null 2>&1 || :

%files
/usr/lib/cups/filter/brother-td-filter
/usr/share/cups/model/brother-td-*.ppd
/usr/share/man/man7/brother-td.7*
/usr/share/man/man7/brother-td-filter.7*
%doc README.md CHANGELOG.md NOTICE AUTHORS docs/protocol.md
%license LICENSE

%files -n brother-td-printer-app
/usr/bin/brother-td-app
/usr/lib/systemd/system/brother-td-app.service
/usr/share/brother-td-app/style.css
/usr/share/man/man1/brother-td-app.1*
%license LICENSE

%changelog
* Thu Nov 19 2026 Jochen Schneider <scne59@gmail.com> - 1.0.0-1
- Initial Linux CUPS driver and PAPPL printer application.
