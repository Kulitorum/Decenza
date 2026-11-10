# Spec Delta

## Purpose

Defines the oldest user data the app upgrades from, how a fresh install reaches that starting point, and how older data is refused untouched at launch and on import.

## ADDED Requirements

### Requirement: The upgrade floor is shot database schema 41
The app SHALL upgrade only data whose shot database is at schema version 41 or higher; no release number defines the floor. Release 2.1.0 SHALL be named as the stepping stone for older data. The floor and the stepping-stone release SHALL each be defined once and used by every check and message below.

#### Scenario: A schema-41 database upgrades
- **WHEN** the app starts on a device whose shot database is at schema 41
- **THEN** migrations 42 and up run and the app starts normally

#### Scenario: A 2.1.0 database upgrades
- **WHEN** the app starts on a device whose shot database is at schema 47
- **THEN** only the migrations above 47 run and the app starts normally

### Requirement: Older data is refused at launch without being modified
Before any settings or database migration runs, the app SHALL read the shot database's schema version. When an existing database is below the floor, the app SHALL show a notice telling the user to install Decenza 2.1.0 first, and SHALL exit when the user confirms. The shot database, the settings store, profiles and media SHALL be left exactly as they were.

#### Scenario: A 1.x database is refused
- **GIVEN** a device whose shot database is at schema 23
- **WHEN** the app starts
- **THEN** the notice names Decenza 2.1.0 as the release to install first
- **AND** the app exits when the user confirms
- **AND** the database file's tables, rows and schema version are unchanged
- **AND** no settings key has been written

#### Scenario: A schema-40 database is refused
- **GIVEN** a device whose shot database is at schema 40
- **WHEN** the app starts
- **THEN** the notice is shown and the app exits on confirm without modifying any data

#### Scenario: Data under the pre-1.5.6 app name is refused
- **GIVEN** a desktop install with no shot database under the current app name and one under "Decenza DE1"
- **WHEN** the app starts
- **THEN** the notice is shown and the app exits without creating a database

#### Scenario: Refused data still upgrades through the stepping stone
- **GIVEN** a device that was refused and then had Decenza 2.1.0 installed and launched once
- **WHEN** the new release is installed again
- **THEN** the app starts normally with the user's shots and settings intact

### Requirement: The notice is reachable with a screen reader
The refusal notice SHALL expose its message and its confirm control to TalkBack and VoiceOver. The confirm control SHALL be focusable and activatable from the accessibility action.

#### Scenario: Confirm with a screen reader
- **WHEN** a screen-reader user reaches the refusal notice
- **THEN** the message is announced
- **AND** activating the confirm control exits the app

### Requirement: An unreadable version does not block startup
When the launch check cannot read a schema version from an existing database, it SHALL NOT show the refusal notice. Startup SHALL continue, and the database layer SHALL refuse to migrate a database below the floor before writing to it.

#### Scenario: Version read fails
- **WHEN** the launch check fails to read the schema version of an existing database
- **THEN** startup continues
- **AND** if the database proves to be below the floor, the database layer stops before changing it and reports the failure

### Requirement: A fresh install starts at the floor schema
When no shot database exists, the app SHALL create the schema-41 tables directly, stamp it at version 41, and then run migrations 42 and up. A fresh install SHALL end with the same tables, columns, indexes and triggers as a schema-41 database upgraded by the same release.

#### Scenario: Fresh install matches an upgraded database
- **GIVEN** a fresh install and a database a fresh install of 2.0.7 created
- **WHEN** both are opened by the new release
- **THEN** both end at the same schema version
- **AND** their tables, columns, indexes and triggers are the same

#### Scenario: Fresh install shows no notice
- **WHEN** the app starts with no shot database present
- **THEN** no refusal notice is shown and the app starts normally

### Requirement: Restores and transfers refuse sources older than the floor
Restoring a backup and receiving a device-to-device transfer SHALL refuse a source whose shot database is below the floor, has no schema version, or cannot be read, with a message naming Decenza 2.1.0 as the release to restore it in. Nothing from that source SHALL be applied, including settings, profiles and media. A backup's shot database SHALL be checked even when shots are not being restored.

#### Scenario: A backup from 1.8 is refused
- **WHEN** the user restores a backup whose shot database is at schema 23
- **THEN** the restore fails with a message naming Decenza 2.1.0
- **AND** the device's shots, settings, profiles and media are unchanged

#### Scenario: A transfer of a schema-40 database is refused
- **WHEN** a device-to-device transfer delivers a shot database at schema 40
- **THEN** the transfer reports the refusal with a message naming Decenza 2.1.0
- **AND** nothing from the source is imported

#### Scenario: A backup restored without its shots is still checked
- **WHEN** the user restores only the settings from a backup whose shot database is at schema 40
- **THEN** the restore fails with a message naming Decenza 2.1.0
- **AND** the device's settings are unchanged

#### Scenario: A backup whose shot database cannot be read is refused
- **WHEN** the user restores only the settings from a backup whose shot database has no `shots` table
- **THEN** the restore fails
- **AND** the device's settings are unchanged

#### Scenario: A schema-41 backup restores
- **WHEN** the user restores a backup whose shot database is at schema 41
- **THEN** the restore proceeds, filling columns the source lacks as it does today

#### Scenario: A settings-only file is not version-gated
- **WHEN** the user imports a settings export that carries no shot database
- **THEN** the import proceeds
- **AND** keys that only releases below the floor wrote are ignored

### Requirement: Transfers check an unreported source's database first
A transfer source reports its schema in its manifest. For a source that does not, its shot database SHALL be checked before any category is applied, whichever categories are imported. A database that cannot be fetched or read SHALL be refused too.

#### Scenario: A source that does not report its schema stops at its database
- **GIVEN** a transfer source whose manifest reports no schema and whose shot database is at schema 40
- **WHEN** the user imports everything from it, or only its settings
- **THEN** the transfer fails with a message naming Decenza 2.1.0
- **AND** no settings are fetched or applied

#### Scenario: An unreported source whose database cannot be fetched is refused
- **GIVEN** a transfer source whose manifest reports no schema and whose shot database cannot be downloaded
- **WHEN** the user imports only its settings
- **THEN** the transfer fails and no settings are fetched or applied
