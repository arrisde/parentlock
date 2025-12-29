# Parent lock software

Thank you for installing Parent Lock software for MuOS (goose).
This application is made to set up and limit device usage time.
The configuration and usage are described below.

## Configuration

The parent lock application is using a configuration file found in `/mnt/mmc/MUOS/application/ParentLock/parent_lock.ini`.
This configuration file looks like this:

```
[code]
unlock=0000

[message]
unlock=Time is up
setting=

[times]
monday=0
tuesday=0
wednesday=0
thursday=0
friday=0
saturday=0
sunday=0
```

The `unlock` key in the `code` section defines the code to enter to unlock the device. Using `0000` disables the parent lock.
When the time is up for a given day, a lock screen will be displayed where it's possible to either unlock (by entering the unlock code and pressing `A`) or shutting the device off (by pressing 'Power' button)

The `unlock` key in the `message` section defines the title of the unlock screen.
The **days** keys in the `times` section contains the allowance, in minutes, for each day. Using `0` or anything above `1440` disable the locking mechanism for this day.

## Usage

When using the parental lock feature, it's mainly passive. Once the time is up, a lock screen is shown where you can enter the code you've set up to unlock or shutdown the device by pressing 'Power' button.

It might be interesting to set up a "Settings" passcode too (see muxpass documentation at https://muos.dev/installation/passcode to learn how to do that) so it's not possible for your kids to change the configuration of the device. You'll need to enable "Custom init script" option in the settings for the application to load on boot. Don't allow your kids to run a terminal and don't allow them to delete files on the device (I'm not telling you which files are important, so they can't know for sure and it'll likely damage the console). You can activate kiosk mode and restrict access to any settings menu (see https://muos.dev/tour/modules/muxkiosk). 

