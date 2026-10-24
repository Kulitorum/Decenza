pragma Singleton
import QtQuick

QtObject {
    function translate(key: string, fallback: string): string { return fallback }
}
