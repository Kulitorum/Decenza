# Spec Delta

## ADDED Requirements

### Requirement: Bag editor excludes equipment and grind
The bag editor SHALL omit equipment, grind and RPM for every bag while retaining the editable bean, dose and yield fields.

#### Scenario: Edit an existing bag
- **WHEN** an existing coffee bag is opened for editing after upgrade
- **THEN** its bean details, dose and yield remain editable and no equipment, grind or RPM field appears
