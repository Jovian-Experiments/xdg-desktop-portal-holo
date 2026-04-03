.. _xdg-desktop-portal-holo-settings(5):
.. meta::
   :copyright: 2025 Valve Corporation


================================
xdg-desktop-portal-holo-settings
================================

settings.conf
-------------

SYNOPSIS
--------

|  $XDG_CONFIG_DIRS/SteamOS/portal/settings.conf
|  $XDG_CONFIG_HOME/SteamOS/portal/settings.conf

DESCRIPTION
-----------

The ``settings.conf`` configuration file specifies the settings for the
Holo xdg-desktop-portal backend.

The format used for the configuration file is a key/value pairs file as
described by the `XDG desktop entry specification <https://specifications.freedesktop.org/desktop-entry-spec/latest/basic-format.html>`_.

KEYS
----

The keys should be placed under a group with the namespace reserved for their
scope.

The following keys must be part of the ``org.freedesktop.appearance`` group:

* **color-scheme**: an unsigned integer with one of these three values:
  * ``0``: no preference
  * ``1``: prefer dark appearance
  * ``2``: prefer light appearance

* **accent-color**: a list of three floating point values indicating the
  system's preferred   accent color; each value in the list must be between
  in the [0, 1] range.

* **contrast**: an unsigned integer value indicating the constrast level; supported
  values are:
  * ``0``: no preference (normal contrast)
  * ``1``: higher contrast


Additional keys under separate namespaces can be exposed under separate groups,
for instance::

    [org.gnome.fontconfig]
    serial = 12345678

    [org.gnome.desktop.interface]
    enable-animations = true

Will expose the ``org.gnome.fontconfig.serial`` and
``org.gnome.desktop.interface.enable-animations`` keys to applications using the
portal.

SEE ALSO
--------

* `Settings portal <https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Settings.html>`_
