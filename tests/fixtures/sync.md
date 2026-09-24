# Проверочный документ

Каждый раздел проверяет одну конструкцию. Номер раздела помогает
проверить синхронизацию позиции: поставьте курсор в разделе, нажмите
`Ctrl+/`, и просмотр должен открыться на том же разделе.

## 1. Вложенные списки

Плотный список, три уровня:

- первый уровень
  - второй уровень
    - третий уровень
    - ещё третий
  - снова второй
- снова первый

Свободный список (пустые строки между пунктами, внутри `<li>` идут `<p>`):

- первый пункт

  второй абзац первого пункта

- второй пункт

  - вложенный пункт

    с абзацем

Нумерованный список, начиная с 3, со вложенным маркированным:

3. третий
4. четвёртый
   - вложенный
   - ещё вложенный
5. пятый

## 2. Цитаты в цитатах

> Первый уровень
>
> > Второй уровень
> >
> > > Третий уровень
>
> Снова первый, а в нём список и код:
>
> - пункт в цитате
> - ещё пункт
>
> ```
> код в цитате
> ```

## 3. Код

Инлайн: `printf("%d\n", x)` посреди текста.

Огороженный блок с языком:

```cpp
int main() {
    return 0;
}
```

Блок с отступом:

    indented code
      ещё отступ

## 4. Сырой HTML

Инлайн: <kbd>Ctrl</kbd>+<kbd>S</kbd>, <span style="color: red">красный span</span>,
<b>b</b>, <sub>sub</sub> и <sup>sup</sup>.

<div style="border: 1px solid gray">
Блок div с рамкой.
</div>

<details>
<summary>Раскрывающийся блок</summary>

Содержимое details.
</details>

<table>
<tr><td>HTML-таблица</td><td>ячейка</td></tr>
</table>

<!-- комментарий, не должен быть виден -->

<script>alert("скрипт не должен выполняться")</script>

<custom-tag>неизвестный тег</custom-tag>

## 5. Честный CommonMark: GFM должен остаться текстом

| Колонка | Колонка |
| ------- | ------- |
| a       | b       |

- [ ] задача
- [x] выполненная задача

~~зачёркнутый~~ и автоссылка www.example.com без угловых скобок.

## 6. Прочее

Ссылки: [внешняя](https://commonmark.org), [на раздел](#1-вложенные-списки),
[на другой .md](other.md), <https://spec.commonmark.org>.

Изображение по относительному пути: ![пиксель](pixel.png)

*курсив*, **жирный**, ***оба***, жёсткий\
перенос строки.

---

Setext-заголовок
----------------

Экранирование: \*не курсив\*, &copy; &amp; &#x41;.

## 7. Длинный хвост для проверки прокрутки

Абзац 1. Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do
eiusmod tempor incididunt ut labore et dolore magna aliqua.

Абзац 2. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris
nisi ut aliquip ex ea commodo consequat.

Абзац 3. Duis aute irure dolor in reprehenderit in voluptate velit esse
cillum dolore eu fugiat nulla pariatur.

Абзац 4. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui
officia deserunt mollit anim id est laborum.

Абзац 5. Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do
eiusmod tempor incididunt ut labore et dolore magna aliqua.

Абзац 6. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris
nisi ut aliquip ex ea commodo consequat.

Абзац 7. Duis aute irure dolor in reprehenderit in voluptate velit esse
cillum dolore eu fugiat nulla pariatur.

Абзац 8. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui
officia deserunt mollit anim id est laborum.
