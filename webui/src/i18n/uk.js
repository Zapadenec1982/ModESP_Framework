export default {
  // App
  'app.loading': 'Завантаження...',
  'app.error': 'Помилка з\'єднання',
  'app.retry': 'Повторити',

  // Layout
  'status.online': 'Онлайн',
  'status.offline': 'Офлайн',
  'alarm.banner': 'ТРИВОГА',

  // Generic state labels
  'eq.on': 'ON',
  'eq.off': 'OFF',

  // Chart
  'chart.loading': 'Завантаження...',
  'chart.no_data': 'Немає даних',
  'chart.events': 'Події',
  'chart.title': 'Температура',
  'event.7': 'Аварія знята',
  'event.10': 'Увімкнення',

  // Bindings
  'bind.status': 'Стан обладнання',
  'bind.loading': 'Завантаження...',
  'bind.saved': 'Збережено. Потрібен перезапуск.',
  'bind.saved_title': 'Збережено',
  'bind.saved_msg': 'Для застосування змін потрібно перезавантажити контролер.',
  'bind.restart': 'Перезавантажити',
  'bind.later': 'Пізніше',
  'bind.required': 'Необхідно',
  'bind.optional': 'Опціонально',
  'bind.save': 'Зберегти',
  'bind.saving': 'Збереження...',
  'bind.scan': 'Сканувати шину',
  'bind.scanning': 'Сканування...',
  'bind.scan_hint': 'Натисніть "Сканувати шину" для пошуку',
  'bind.all_assigned': 'Всі пристрої вже призначені',
  'bind.unassigned': 'Не призначено',
  'bind.add': 'Додати',
  'bind.sensors': 'Датчики',
  'bind.actuators': 'Приводи',
  'bind.onewire': 'Виявлення OneWire',
  'bind.add_equip': 'Додати обладнання',
  'bind.used': 'зайнято',
  'bind.choose_hw': 'Оберіть обладнання',
  'bind.choose_channel': 'Оберіть канал',
  'bind.role': '-- Роль --',
  'bind.found': 'Знайдено {0} пристроїв, {1} вже призначено',
  'bind.found_total': 'Знайдено на шині:',
  'bind.new_device': 'новий',
  'bind.unbind': 'Відкріпити',
  'bind.no_free_roles': 'Немає вільних ролей',
  'bind.confirm_missing': 'Відсутні обов\'язкові ролі',
  'bind.confirm_alarm': 'Система запуститься в аварійному режимі. Продовжити?',
  'bind.pick': 'Обрати',
  'bind.in_use': 'зайнято',
  'bind.selected': 'обрано',
  'bind.no_devices': 'Пристроїв не знайдено',

  // Devices (remote device subscriptions)
  'dev.loading': 'Завантаження…',
  'dev.load_failed': 'Не вдалося завантажити пристрої',
  'dev.subscribed': 'Підписані пристрої',
  'dev.none': 'Немає підписаних пристроїв. Скануйте нижче, щоб додати.',
  'dev.scan': 'Сканувати',
  'dev.scanning': 'Сканування…',
  'dev.scan_title': 'Пошук BLE-пристроїв',
  'dev.scan_desc': 'Показано лише пристрої, які розуміють наші драйвери. Розрізняйте однотипні за показами (нахиліть/наблизьте потрібний) чи за RSSI.',
  'dev.scan_hint': 'Натисніть «Сканувати» для пошуку пристроїв поблизу',
  'dev.none_found': 'Підтримуваних пристроїв поблизу не знайдено',
  'dev.subscribe': 'Підписати',
  'dev.have': 'підписано',
  'dev.save': 'Зберегти',
  'dev.saving': 'Збереження…',
  'dev.unsaved': 'Є незбережені зміни',
  'dev.saved_title': 'Пристрої збережено',
  'dev.saved_msg': 'Зміни застосуються після перезапуску.',

  'page.not_found': 'Сторінку не знайдено',

  // Connection
  'conn.lost': 'З\'єднання втрачено. Перепідключення...',
  'conn.retry': 'Перепідключити',
  'conn.restored': 'З\'єднано',

  // Alerts
  'alert.saved': 'Збережено!',
  'alert.saved_restart': 'Збережено! Перезавантажте.',
  'alert.saved_mqtt': 'Збережено! MQTT перепідключується...',
  'alert.error': 'Помилка збереження',
  'alert.conn_error': 'Помилка з\'єднання',
  'alert.invalid_value': 'Некоректне значення',
  'alert.ssid_empty': 'SSID не може бути порожнім',
  'alert.pass_min8': 'Пароль мінімум 8 символів',
  'alert.confirm_ota': 'Оновити прошивку? Пристрій перезавантажиться.',
  'alert.only_bin': 'Тільки .bin файли',

  // OTA
  'ota.uploading': 'Завантаження...',
  'ota.done': 'Готово! Перезапуск...',
  'ota.upload': 'Оновити прошивку',
  'ota.select': 'Обрати файл прошивки',
  'ota.restarting': 'Перезапуск через {0}с...',
  'ota.too_large': 'Файл занадто великий (макс. ~1.4 MB)',
  'ota.board_mismatch': 'Прошивка для іншої плати: {incoming} (потрібна {running})',

  // WiFi
  'wifi.scan': 'Сканувати',
  'wifi.scanning': 'Сканування...',

  // Timezone
  'tz.label': 'Часовий пояс',

  // Password
  'pass.show': 'Показати',
  'pass.hide': 'Сховати',

  // Auth
  'auth.title': 'Авторизація',
  'auth.user': 'Користувач',
  'auth.pass': 'Пароль',
  'auth.login': 'Увійти',
  'auth.error': 'Невірний логін або пароль',
  'auth.enabled': 'Аутентифікація увімкнена',
  'auth.saved': 'Налаштування збережено',

  // Navigation
  'nav.more': 'Ще',

  // Buttons
  'btn.action': 'Дія',
  'btn.save': 'Зберегти',
  'btn.save_ap': 'Зберегти AP',
  'btn.error': 'Помилка',
  'btn.remove': 'Видалити',

  // Equipment / driver settings
  'eq.filter': 'Цифровий фільтр',
  'eq.offset': 'Корекція °C',

  // Chart
  'chart.min_ago': 'хв тому',
};
