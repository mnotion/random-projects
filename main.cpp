#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
constexpr int kDefaultDpi = 300;
constexpr quint64 kMaxPixelsPerPage = 65000000ULL;
constexpr quint64 kMaxZip32Size = 0xFFFFFFFFULL;
constexpr quint16 kZipUtf8Flag = 0x0800;
constexpr quint16 kZipDosDate = static_cast<quint16>(((2024 - 1980) << 9) | (1 << 5) | 1);

struct PageImage {
    QString path;
    QSizeF pointSize;
    QSize pixelSize;
};

struct ZipEntry {
    QString archivePath;
    QByteArray data;
    QString sourceFile;
};

struct ZipCentralEntry {
    QByteArray name;
    quint32 crc = 0;
    quint32 size = 0;
    quint32 localOffset = 0;
};

[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toUtf8().constData());
}

bool writeAll(QIODevice &device, const char *data, qint64 size)
{
    qint64 written = 0;
    while (written < size) {
        const qint64 current = device.write(data + written, size - written);
        if (current <= 0)
            return false;
        written += current;
    }
    return true;
}

bool writeAll(QIODevice &device, const QByteArray &data)
{
    return writeAll(device, data.constData(), data.size());
}

void writeU16(QIODevice &device, quint16 value)
{
    char bytes[2] = {
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF)
    };
    if (!writeAll(device, bytes, 2))
        fail(QStringLiteral("Could not write ZIP archive."));
}

void writeU32(QIODevice &device, quint32 value)
{
    char bytes[4] = {
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >> 24) & 0xFF)
    };
    if (!writeAll(device, bytes, 4))
        fail(QStringLiteral("Could not write ZIP archive."));
}

quint32 crc32Update(quint32 crc, const char *data, qsizetype length)
{
    for (qsizetype i = 0; i < length; ++i) {
        crc ^= static_cast<unsigned char>(data[i]);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}

quint32 crc32ForBytes(const QByteArray &data)
{
    return crc32Update(0xFFFFFFFFU, data.constData(), data.size()) ^ 0xFFFFFFFFU;
}

quint32 crc32ForFile(const QString &path, quint32 *sizeOut)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(QStringLiteral("Cannot read temporary PNG: %1").arg(path));

    const qint64 fileSize = file.size();
    if (fileSize < 0 || static_cast<quint64>(fileSize) > kMaxZip32Size)
        fail(QStringLiteral("A generated PNG is too large for the DOCX package format."));

    quint32 crc = 0xFFFFFFFFU;
    char buffer[1024 * 1024];
    while (!file.atEnd()) {
        const qint64 read = file.read(buffer, sizeof(buffer));
        if (read < 0)
            fail(QStringLiteral("Failed while reading a generated PNG: %1").arg(path));
        if (read == 0)
            break;
        crc = crc32Update(crc, buffer, read);
    }

    if (sizeOut)
        *sizeOut = static_cast<quint32>(fileSize);
    return crc ^ 0xFFFFFFFFU;
}

void copyFileBytes(QIODevice &out, const QString &path)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        fail(QStringLiteral("Cannot open generated file: %1").arg(path));

    char buffer[1024 * 1024];
    while (!input.atEnd()) {
        const qint64 read = input.read(buffer, sizeof(buffer));
        if (read < 0)
            fail(QStringLiteral("Failed while reading generated file: %1").arg(path));
        if (read == 0)
            break;
        if (!writeAll(out, buffer, read))
            fail(QStringLiteral("Failed while writing DOCX data."));
    }
}

void writeStoredZip(const QString &destination, const std::vector<ZipEntry> &entries)
{
    if (entries.size() > std::numeric_limits<quint16>::max())
        fail(QStringLiteral("This document has too many page images for a DOCX package."));

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly))
        fail(QStringLiteral("Cannot create output file: %1").arg(destination));

    try {
        std::vector<ZipCentralEntry> central;
        central.reserve(entries.size());

        for (const ZipEntry &entry : entries) {
            const QByteArray name = entry.archivePath.toUtf8();
            if (name.isEmpty() || name.size() > std::numeric_limits<quint16>::max())
                fail(QStringLiteral("Invalid filename inside the DOCX package."));

            quint32 size = 0;
            quint32 crc = 0;
            if (!entry.sourceFile.isEmpty()) {
                crc = crc32ForFile(entry.sourceFile, &size);
            } else {
                if (static_cast<quint64>(entry.data.size()) > kMaxZip32Size)
                    fail(QStringLiteral("A DOCX package component is too large."));
                size = static_cast<quint32>(entry.data.size());
                crc = crc32ForBytes(entry.data);
            }

            const qint64 offset64 = output.pos();
            if (offset64 < 0 || static_cast<quint64>(offset64) > kMaxZip32Size)
                fail(QStringLiteral("The DOCX package exceeds the ZIP32 size limit."));
            const quint32 offset = static_cast<quint32>(offset64);

            writeU32(output, 0x04034B50U);
            writeU16(output, 20);
            writeU16(output, kZipUtf8Flag);
            writeU16(output, 0); // Store: no compression or quality loss.
            writeU16(output, 0); // DOS time.
            writeU16(output, kZipDosDate);
            writeU32(output, crc);
            writeU32(output, size);
            writeU32(output, size);
            writeU16(output, static_cast<quint16>(name.size()));
            writeU16(output, 0);
            if (!writeAll(output, name))
                fail(QStringLiteral("Could not write ZIP filename."));
            if (!entry.sourceFile.isEmpty())
                copyFileBytes(output, entry.sourceFile);
            else if (!writeAll(output, entry.data))
                fail(QStringLiteral("Could not write DOCX package component."));

            central.push_back({name, crc, size, offset});
        }

        const qint64 centralStart64 = output.pos();
        if (centralStart64 < 0 || static_cast<quint64>(centralStart64) > kMaxZip32Size)
            fail(QStringLiteral("The DOCX package exceeds the ZIP32 size limit."));
        const quint32 centralStart = static_cast<quint32>(centralStart64);

        for (const ZipCentralEntry &item : central) {
            writeU32(output, 0x02014B50U);
            writeU16(output, 20); // Version made by.
            writeU16(output, 20); // Version needed.
            writeU16(output, kZipUtf8Flag);
            writeU16(output, 0); // Store.
            writeU16(output, 0);
            writeU16(output, kZipDosDate);
            writeU32(output, item.crc);
            writeU32(output, item.size);
            writeU32(output, item.size);
            writeU16(output, static_cast<quint16>(item.name.size()));
            writeU16(output, 0); // Extra field.
            writeU16(output, 0); // Comment.
            writeU16(output, 0); // Disk number.
            writeU16(output, 0); // Internal attributes.
            writeU32(output, 0); // External attributes.
            writeU32(output, item.localOffset);
            if (!writeAll(output, item.name))
                fail(QStringLiteral("Could not write ZIP directory entry."));
        }

        const qint64 centralEnd = output.pos();
        const qint64 centralSize64 = centralEnd - centralStart64;
        if (centralSize64 < 0 || static_cast<quint64>(centralSize64) > kMaxZip32Size)
            fail(QStringLiteral("The DOCX package exceeds the ZIP32 size limit."));

        writeU32(output, 0x06054B50U);
        writeU16(output, 0);
        writeU16(output, 0);
        writeU16(output, static_cast<quint16>(central.size()));
        writeU16(output, static_cast<quint16>(central.size()));
        writeU32(output, static_cast<quint32>(centralSize64));
        writeU32(output, centralStart);
        writeU16(output, 0);

        if (!output.commit())
            fail(QStringLiteral("Could not finish writing the DOCX file: %1").arg(destination));
    } catch (...) {
        output.cancelWriting();
        throw;
    }
}

QImage renderPage(QPdfDocument &document, int pageIndex, int dpi)
{
    const QSizeF points = document.pagePointSize(pageIndex);
    if (points.width() <= 0 || points.height() <= 0)
        fail(QStringLiteral("PDF page %1 has an invalid size.").arg(pageIndex + 1));

    const qint64 width = qMax<qint64>(1, qRound64(points.width() * dpi / 72.0));
    const qint64 height = qMax<qint64>(1, qRound64(points.height() * dpi / 72.0));
    if (width > 30000 || height > 30000 ||
        static_cast<quint64>(width) * static_cast<quint64>(height) > kMaxPixelsPerPage) {
        fail(QStringLiteral("Page %1 would be too large at %2 DPI. Choose a lower DPI.")
                 .arg(pageIndex + 1).arg(dpi));
    }

    QImage raw = document.render(pageIndex, QSize(static_cast<int>(width), static_cast<int>(height)));
    if (raw.isNull())
        fail(QStringLiteral("Qt could not render PDF page %1.").arg(pageIndex + 1));

    // Some PDFs render with transparent background. Flatten onto white for a normal printed page.
    QImage opaque(raw.size(), QImage::Format_RGB32);
    opaque.fill(Qt::white);
    {
        QPainter painter(&opaque);
        painter.drawImage(0, 0, raw);
    }
    return opaque;
}

void requirePdfLoaded(QPdfDocument &document, const QString &path)
{
    const QPdfDocument::Error error = document.load(path);
    if (error != QPdfDocument::Error::None) {
        fail(QStringLiteral("Could not open PDF: %1 (Qt PDF error %2).")
                 .arg(path).arg(static_cast<int>(error)));
    }
    if (document.pageCount() < 1)
        fail(QStringLiteral("The PDF has no readable pages: %1").arg(path));
}

QString sectionProperties(const QSizeF &pagePoints, bool nextPage)
{
    const double limitPoints = 22.0 * 72.0; // Word limits a page dimension to 22 inches.
    const double scale = qMin(1.0, limitPoints / qMax(pagePoints.width(), pagePoints.height()));
    const double widthPoints = pagePoints.width() * scale;
    const double heightPoints = pagePoints.height() * scale;
    const quint32 widthTwips = static_cast<quint32>(qMax(1, qRound(widthPoints * 20.0)));
    const quint32 heightTwips = static_cast<quint32>(qMax(1, qRound(heightPoints * 20.0)));
    const QString orientation = widthPoints > heightPoints
        ? QStringLiteral(" w:orient=\"landscape\"")
        : QString();

    QString xml = QStringLiteral("<w:sectPr>");
    if (nextPage)
        xml += QStringLiteral("<w:type w:val=\"nextPage\"/>");
    xml += QStringLiteral("<w:pgSz w:w=\"%1\" w:h=\"%2\"%3/>")
               .arg(widthTwips).arg(heightTwips).arg(orientation);
    xml += QStringLiteral("<w:pgMar w:top=\"0\" w:right=\"0\" w:bottom=\"0\" w:left=\"0\" w:header=\"0\" w:footer=\"0\" w:gutter=\"0\"/>");
    xml += QStringLiteral("</w:sectPr>");
    return xml;
}

QString imageParagraph(const PageImage &page, int pageNumber, bool endsSection)
{
    const double limitPoints = 22.0 * 72.0;
    const double pageScale = qMin(1.0, limitPoints /
                                      qMax(page.pointSize.width(), page.pointSize.height()));
    const double pageWidthPoints = page.pointSize.width() * pageScale;
    const double pageHeightPoints = page.pointSize.height() * pageScale;

    const double pxWidth = qMax(1, page.pixelSize.width());
    const double pxHeight = qMax(1, page.pixelSize.height());
    const double availableWidthEmu = pageWidthPoints * 12700.0;
    const double availableHeightEmu = qMax(1.0, pageHeightPoints - 1.0) * 12700.0;
    const double imageScale = qMin(availableWidthEmu / pxWidth, availableHeightEmu / pxHeight);
    const quint64 cx = static_cast<quint64>(qMax<qint64>(1, qRound64(pxWidth * imageScale)));
    const quint64 cy = static_cast<quint64>(qMax<qint64>(1, qRound64(pxHeight * imageScale)));

    QString xml = QStringLiteral("<w:p><w:pPr><w:spacing w:before=\"0\" w:after=\"0\" w:line=\"20\" w:lineRule=\"exact\"/><w:jc w:val=\"center\"/>");
    if (endsSection)
        xml += sectionProperties(page.pointSize, true);
    xml += QStringLiteral("</w:pPr><w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">");
    xml += QStringLiteral("<wp:extent cx=\"%1\" cy=\"%2\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>")
               .arg(cx).arg(cy);
    xml += QStringLiteral("<wp:docPr id=\"%1\" name=\"Page %1\" descr=\"Full-page PNG rendering\"/>").arg(pageNumber);
    xml += QStringLiteral("<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>");
    xml += QStringLiteral("<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic>");
    xml += QStringLiteral("<pic:nvPicPr><pic:cNvPr id=\"%1\" name=\"page-%1.png\"/><pic:cNvPicPr/></pic:nvPicPr>").arg(pageNumber);
    xml += QStringLiteral("<pic:blipFill><a:blip r:embed=\"rId%1\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>").arg(pageNumber);
    xml += QStringLiteral("<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>")
               .arg(cx).arg(cy);
    xml += QStringLiteral("</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>");
    return xml;
}

void createDocxFromPngPages(const QString &destination, const std::vector<PageImage> &pages)
{
    if (pages.empty())
        fail(QStringLiteral("There are no pages to put into Word."));

    QString documentXml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\""
        " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\""
        " xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\""
        " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
        " xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><w:body>");

    QString relationships = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rIdStyles\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>");

    std::vector<ZipEntry> entries;
    entries.reserve(pages.size() + 5);

    for (size_t i = 0; i < pages.size(); ++i) {
        const int pageNumber = static_cast<int>(i + 1);
        documentXml += imageParagraph(pages[i], pageNumber, i + 1 < pages.size());
        const QString archiveImageName = QStringLiteral("page-%1.png").arg(pageNumber, 5, 10, QLatin1Char('0'));
        relationships += QStringLiteral(
            "<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/%2\"/>")
                             .arg(pageNumber).arg(archiveImageName);
        entries.push_back({QStringLiteral("word/media/") + archiveImageName, QByteArray(), pages[i].path});
    }

    documentXml += sectionProperties(pages.back().pointSize, false);
    documentXml += QStringLiteral("</w:body></w:document>");
    relationships += QStringLiteral("</Relationships>");

    const QByteArray contentTypes = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "</Types>");

    const QByteArray rootRelationships = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>");

    const QByteArray styles = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:docDefaults><w:rPrDefault><w:rPr><w:lang w:val=\"en-GB\"/></w:rPr></w:rPrDefault>"
        "<w:pPrDefault><w:pPr/></w:pPrDefault></w:docDefaults>"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>"
        "</w:styles>");

    entries.push_back({QStringLiteral("[Content_Types].xml"), contentTypes, QString()});
    entries.push_back({QStringLiteral("_rels/.rels"), rootRelationships, QString()});
    entries.push_back({QStringLiteral("word/document.xml"), documentXml.toUtf8(), QString()});
    entries.push_back({QStringLiteral("word/styles.xml"), styles, QString()});
    entries.push_back({QStringLiteral("word/_rels/document.xml.rels"), relationships.toUtf8(), QString()});

    // Keep required package parts ahead of the media files for predictable inspection.
    std::stable_sort(entries.begin(), entries.end(), [](const ZipEntry &a, const ZipEntry &b) {
        const bool aMedia = a.archivePath.startsWith(QStringLiteral("word/media/"));
        const bool bMedia = b.archivePath.startsWith(QStringLiteral("word/media/"));
        return !aMedia && bMedia;
    });

    writeStoredZip(destination, entries);
}

void convertPdfToWord(const QString &source, const QString &destination, int dpi, const QString &tempRoot)
{
    QPdfDocument pdf;
    requirePdfLoaded(pdf, source);

    const QString pagesDirectory = QDir(tempRoot).filePath(QStringLiteral("png-pages"));
    if (!QDir().mkpath(pagesDirectory))
        fail(QStringLiteral("Could not create a temporary PNG directory."));

    std::vector<PageImage> pages;
    pages.reserve(static_cast<size_t>(pdf.pageCount()));
    for (int i = 0; i < pdf.pageCount(); ++i) {
        QImage image = renderPage(pdf, i, dpi);
        const QString imagePath = QDir(pagesDirectory).filePath(
            QStringLiteral("page-%1.png").arg(i + 1, 5, 10, QLatin1Char('0')));
        if (!image.save(imagePath, "PNG"))
            fail(QStringLiteral("Could not save temporary PNG for page %1.").arg(i + 1));
        pages.push_back({imagePath, pdf.pagePointSize(i), image.size()});
    }

    createDocxFromPngPages(destination, pages);
}

QString findLibreOffice()
{
    QString executable = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (!executable.isEmpty())
        return executable;

#ifdef Q_OS_WIN
    const QString programFiles = qEnvironmentVariable("PROGRAMFILES");
    const QString programFilesX86 = qEnvironmentVariable("PROGRAMFILES(X86)");
    const QStringList candidates = {
        QDir(programFiles).filePath(QStringLiteral("LibreOffice/program/soffice.exe")),
        QDir(programFilesX86).filePath(QStringLiteral("LibreOffice/program/soffice.exe"))
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
#elif defined(Q_OS_MACOS)
    const QString candidate = QStringLiteral("/Applications/LibreOffice.app/Contents/MacOS/soffice");
    if (QFileInfo(candidate).isFile())
        return candidate;
#endif
    return {};
}

QString convertWordToTemporaryPdf(const QString &source, const QString &tempDirectory, const QString &libreOffice)
{
    const QString profileDirectory = QDir(tempDirectory).filePath(QStringLiteral("lo-profile"));
    if (!QDir().mkpath(profileDirectory))
        fail(QStringLiteral("Could not create a temporary LibreOffice profile."));

    const QString profileUrl = QUrl::fromLocalFile(profileDirectory).toString(QUrl::FullyEncoded);
    QProcess process;
    process.setProgram(libreOffice);
    process.setArguments({
        QStringLiteral("--headless"),
        QStringLiteral("-env:UserInstallation=") + profileUrl,
        QStringLiteral("--convert-to"),
        QStringLiteral("pdf"),
        QStringLiteral("--outdir"),
        tempDirectory,
        source
    });
    process.start();

    if (!process.waitForStarted(10000))
        fail(QStringLiteral("LibreOffice could not be started. Check the LibreOffice installation."));
    if (!process.waitForFinished(300000)) {
        process.kill();
        process.waitForFinished();
        fail(QStringLiteral("LibreOffice took too long to convert: %1").arg(source));
    }

    const QString outputPdf = QDir(tempDirectory).filePath(QFileInfo(source).completeBaseName() + QStringLiteral(".pdf"));
    if (process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0 ||
        !QFileInfo(outputPdf).isFile()) {
        QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (detail.isEmpty())
            detail = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (detail.isEmpty())
            detail = QStringLiteral("LibreOffice did not produce a PDF.");
        fail(QStringLiteral("Word-to-PDF conversion failed for %1: %2").arg(source, detail));
    }
    return outputPdf;
}

QPageLayout pageLayoutForPoints(const QSizeF &points)
{
    // QPageSize stores custom sizes in portrait orientation; rotate the layout for landscape pages.
    const bool landscape = points.width() > points.height();
    const QSizeF portraitSize = landscape ? QSizeF(points.height(), points.width()) : points;
    const QPageSize pageSize(portraitSize, QPageSize::Point,
                             QStringLiteral("PageConvert custom page"), QPageSize::ExactMatch);
    return QPageLayout(pageSize,
                       landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                       QMarginsF(0, 0, 0, 0),
                       QPageLayout::Point);
}

void convertPdfToImageOnlyPdf(const QString &sourcePdf, const QString &destination, int dpi)
{
    QPdfDocument pdf;
    requirePdfLoaded(pdf, sourcePdf);

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly))
        fail(QStringLiteral("Cannot create output PDF: %1").arg(destination));

    try {
        {
            QPdfWriter writer(&output);
            writer.setResolution(dpi);
            writer.setTitle(QFileInfo(sourcePdf).completeBaseName());
            writer.setCreator(QStringLiteral("PageConvert"));
            if (!writer.setPageLayout(pageLayoutForPoints(pdf.pagePointSize(0))))
                fail(QStringLiteral("Qt could not set the PDF page size."));

            QPainter painter(&writer);
            if (!painter.isActive())
                fail(QStringLiteral("Qt could not start writing the output PDF."));
            painter.setRenderHint(QPainter::LosslessImageRendering, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

            for (int i = 0; i < pdf.pageCount(); ++i) {
                if (i > 0) {
                    if (!writer.setPageLayout(pageLayoutForPoints(pdf.pagePointSize(i))))
                        fail(QStringLiteral("Qt could not set the page size for page %1.").arg(i + 1));
                    if (!writer.newPage())
                        fail(QStringLiteral("Qt could not add output PDF page %1.").arg(i + 1));
                }

                QImage image = renderPage(pdf, i, dpi);
                painter.drawImage(writer.pageLayout().fullRectPixels(dpi), image);
            }
            painter.end();
        }

        if (!output.commit())
            fail(QStringLiteral("Could not finish writing output PDF: %1").arg(destination));
    } catch (...) {
        output.cancelWriting();
        throw;
    }
}

QString outputDirectoryName(bool pdfToWord)
{
    return pdfToWord ? QStringLiteral("Converted_Word") : QStringLiteral("Converted_PDF");
}

bool isGeneratedOutputPath(const QString &absolutePath, const QString &root)
{
    const QString cleanPath = QDir::fromNativeSeparators(QDir::cleanPath(absolutePath));
    const QString cleanRoot = QDir::fromNativeSeparators(QDir::cleanPath(root));
    for (const QString &name : {QStringLiteral("Converted_Word"), QStringLiteral("Converted_PDF")}) {
        const QString excluded = cleanRoot + QLatin1Char('/') + name;
#ifdef Q_OS_WIN
        if (cleanPath.startsWith(excluded + QLatin1Char('/'), Qt::CaseInsensitive))
            return true;
#else
        if (cleanPath.startsWith(excluded + QLatin1Char('/')))
            return true;
#endif
    }
    return false;
}

bool supportedFile(const QString &path, bool pdfToWord)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (pdfToWord)
        return suffix == QStringLiteral("pdf");

    static const QStringList wordExtensions = {
        QStringLiteral("docx"), QStringLiteral("doc"), QStringLiteral("docm"),
        QStringLiteral("rtf"), QStringLiteral("odt"), QStringLiteral("ott"),
        QStringLiteral("fodt")
    };
    return wordExtensions.contains(suffix);
}

QString relativePath(const QString &root, const QString &filePath)
{
    return QDir(root).relativeFilePath(filePath);
}

QString uniqueOutputPath(const QString &outputRoot,
                         const QString &relativeSource,
                         const QString &outputSuffix,
                         QSet<QString> *reserved)
{
    QFileInfo info(relativeSource);
    QString base = info.completeBaseName();
    const QString relativeDirectory = info.path() == QStringLiteral(".") ? QString() : info.path();
    QString candidateName = base + outputSuffix;
    auto buildCandidate = [&]() {
        return QDir(outputRoot).filePath(relativeDirectory.isEmpty()
                    ? candidateName
                    : QDir(relativeDirectory).filePath(candidateName));
    };
    QString candidate = buildCandidate();
    QString key = QDir::fromNativeSeparators(QDir::cleanPath(candidate)).toCaseFolded();
    if (!reserved->contains(key)) {
        reserved->insert(key);
        return candidate;
    }

    candidateName = base + QStringLiteral("_") + info.suffix().toLower() + outputSuffix;
    int index = 2;
    do {
        candidate = buildCandidate();
        key = QDir::fromNativeSeparators(QDir::cleanPath(candidate)).toCaseFolded();
        if (!reserved->contains(key))
            break;
        candidateName = base + QStringLiteral("_") + info.suffix().toLower()
                        + QStringLiteral("_%1").arg(index++) + outputSuffix;
    } while (true);
    reserved->insert(key);
    return candidate;
}

class BatchWorker final : public QObject {
    Q_OBJECT

public:
    BatchWorker(QString root, bool pdfToWord, bool recursive, int dpi, QString libreOffice)
        : m_root(QDir::cleanPath(std::move(root))),
          m_pdfToWord(pdfToWord),
          m_recursive(recursive),
          m_dpi(dpi),
          m_libreOffice(std::move(libreOffice))
    {
    }

signals:
    void logMessage(const QString &message);
    void progressChanged(int done, int total);
    void finished(int succeeded, int failed, int total, const QString &outputDirectory);

public slots:
    void run()
    {
        const QString outputRoot = QDir(m_root).filePath(outputDirectoryName(m_pdfToWord));
        if (!QDir().mkpath(outputRoot)) {
            emit logMessage(QStringLiteral("ERROR: Could not create output folder: %1").arg(outputRoot));
            emit finished(0, 0, 0, outputRoot);
            return;
        }

        QStringList files;
        QDirIterator::IteratorFlags flags = m_recursive
            ? QDirIterator::Subdirectories
            : QDirIterator::NoIteratorFlags;
        QDirIterator iterator(m_root, QDir::Files | QDir::NoDotAndDotDot, flags);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            if (isGeneratedOutputPath(path, m_root))
                continue;
            if (supportedFile(path, m_pdfToWord))
                files.push_back(QDir::cleanPath(path));
        }
        std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
            return QString::compare(a, b, Qt::CaseInsensitive) < 0;
        });

        if (files.isEmpty()) {
            emit logMessage(m_pdfToWord
                ? QStringLiteral("No PDF files were found in the selected folder.")
                : QStringLiteral("No supported Word-compatible files were found in the selected folder."));
            emit progressChanged(0, 0);
            emit finished(0, 0, 0, outputRoot);
            return;
        }

        emit logMessage(QStringLiteral("Found %1 file(s). Output folder: %2").arg(files.size()).arg(outputRoot));
        emit progressChanged(0, files.size());

        int succeeded = 0;
        int failed = 0;
        QSet<QString> reservedTargets;

        for (int i = 0; i < files.size(); ++i) {
            const QString source = files.at(i);
            try {
                const QString relative = relativePath(m_root, source);
                const QString outputSuffix = m_pdfToWord ? QStringLiteral(".docx") : QStringLiteral(".pdf");
                const QString destination = uniqueOutputPath(outputRoot, relative, outputSuffix, &reservedTargets);
                if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
                    fail(QStringLiteral("Could not create output subfolder."));

                QTemporaryDir temp;
                if (!temp.isValid())
                    fail(QStringLiteral("Could not create a temporary working directory."));

                if (m_pdfToWord) {
                    convertPdfToWord(source, destination, m_dpi, temp.path());
                    emit logMessage(QStringLiteral("[%1/%2] OK  %3 -> %4 (PNG pages, no OCR)")
                                        .arg(i + 1).arg(files.size()).arg(relative, QDir(outputRoot).relativeFilePath(destination)));
                } else {
                    const QString intermediatePdf = convertWordToTemporaryPdf(source, temp.path(), m_libreOffice);
                    convertPdfToImageOnlyPdf(intermediatePdf, destination, m_dpi);
                    emit logMessage(QStringLiteral("[%1/%2] OK  %3 -> %4 (image-only PDF, no OCR)")
                                        .arg(i + 1).arg(files.size()).arg(relative, QDir(outputRoot).relativeFilePath(destination)));
                }
                ++succeeded;
            } catch (const std::exception &error) {
                ++failed;
                emit logMessage(QStringLiteral("[%1/%2] FAILED  %3 — %4")
                                    .arg(i + 1).arg(files.size()).arg(source, QString::fromUtf8(error.what())));
            } catch (...) {
                ++failed;
                emit logMessage(QStringLiteral("[%1/%2] FAILED  %3 — Unknown error")
                                    .arg(i + 1).arg(files.size()).arg(source));
            }
            emit progressChanged(i + 1, files.size());
        }

        emit logMessage(QStringLiteral("Finished: %1 converted, %2 failed.")
                            .arg(succeeded).arg(failed));
        emit finished(succeeded, failed, files.size(), outputRoot);
    }

private:
    QString m_root;
    bool m_pdfToWord = true;
    bool m_recursive = true;
    int m_dpi = kDefaultDpi;
    QString m_libreOffice;
};

class MainWindow final : public QMainWindow {
public:
    MainWindow()
    {
        setWindowTitle(QStringLiteral("PageConvert — PDF ↔ Word"));
        resize(780, 610);
        setMinimumSize(680, 510);

        auto *central = new QWidget(this);
        auto *layout = new QVBoxLayout(central);
        layout->setContentsMargins(20, 18, 20, 18);
        layout->setSpacing(10);

        auto *title = new QLabel(QStringLiteral("PageConvert"), central);
        QFont titleFont = title->font();
        titleFont.setPointSize(titleFont.pointSize() + 8);
        titleFont.setBold(true);
        title->setFont(titleFont);
        layout->addWidget(title);

        auto *description = new QLabel(
            QStringLiteral("Batch-convert a folder. Each output page is a PNG image; no OCR or text extraction is performed."),
            central);
        description->setWordWrap(true);
        layout->addWidget(description);

        auto *folderLabel = new QLabel(QStringLiteral("Folder containing your files"), central);
        layout->addWidget(folderLabel);

        auto *folderRow = new QHBoxLayout();
        m_folder = new QLineEdit(central);
        m_folder->setPlaceholderText(QStringLiteral("Choose a folder…"));
        auto *browse = new QPushButton(QStringLiteral("Browse…"), central);
        folderRow->addWidget(m_folder, 1);
        folderRow->addWidget(browse);
        layout->addLayout(folderRow);

        auto *options = new QHBoxLayout();
        auto *directionLabel = new QLabel(QStringLiteral("Direction"), central);
        m_direction = new QComboBox(central);
        m_direction->addItem(QStringLiteral("PDF → Word (.docx, PNG pages)"), true);
        m_direction->addItem(QStringLiteral("Word → PDF (image-only pages)"), false);
        auto *dpiLabel = new QLabel(QStringLiteral("Render quality"), central);
        m_dpi = new QComboBox(central);
        m_dpi->addItem(QStringLiteral("150 DPI — smaller files"), 150);
        m_dpi->addItem(QStringLiteral("300 DPI — recommended"), 300);
        m_dpi->addItem(QStringLiteral("600 DPI — high detail"), 600);
        m_dpi->setCurrentIndex(1);
        options->addWidget(directionLabel);
        options->addWidget(m_direction, 1);
        options->addSpacing(8);
        options->addWidget(dpiLabel);
        options->addWidget(m_dpi);
        layout->addLayout(options);

        m_recursive = new QCheckBox(QStringLiteral("Include subfolders and preserve their folder structure"), central);
        m_recursive->setChecked(true);
        layout->addWidget(m_recursive);

        auto *buttonRow = new QHBoxLayout();
        m_convert = new QPushButton(QStringLiteral("Convert folder"), central);
        m_convert->setMinimumHeight(36);
        buttonRow->addWidget(m_convert);
        buttonRow->addStretch(1);
        layout->addLayout(buttonRow);

        m_progress = new QProgressBar(central);
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        layout->addWidget(m_progress);

        auto *activity = new QLabel(QStringLiteral("Activity"), central);
        layout->addWidget(activity);
        m_log = new QPlainTextEdit(central);
        m_log->setReadOnly(true);
        m_log->setPlaceholderText(QStringLiteral("Conversion results will appear here."));
        layout->addWidget(m_log, 1);

        auto *footnote = new QLabel(
            QStringLiteral("PNG compression is lossless. Rasterising a document is not the same as preserving its original vector/text data. Word → PDF requires LibreOffice."),
            central);
        footnote->setWordWrap(true);
        layout->addWidget(footnote);

        setCentralWidget(central);

        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString selected = QFileDialog::getExistingDirectory(
                this, QStringLiteral("Choose folder containing documents"), m_folder->text());
            if (!selected.isEmpty())
                m_folder->setText(selected);
        });
        connect(m_convert, &QPushButton::clicked, this, [this]() { startConversion(); });
        connect(m_direction, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
            updateDependencyHint();
        });
        updateDependencyHint();
    }

private:
    void updateDependencyHint()
    {
        if (m_direction->currentData().toBool()) {
            statusBarHint(QStringLiteral("PDF → Word renders every PDF page to a PNG and embeds each PNG as a full-page image."));
        } else {
            statusBarHint(QStringLiteral("Word → PDF uses LibreOffice for layout, then rasterises its PDF output into PNG-backed PDF pages."));
        }
    }

    void statusBarHint(const QString &text)
    {
        if (statusBar())
            statusBar()->showMessage(text);
        else
            statusBar()->setToolTip(text);
    }

    void startConversion()
    {
        if (m_thread && m_thread->isRunning())
            return;

        const QString root = QDir::cleanPath(m_folder->text().trimmed());
        if (root.isEmpty() || !QFileInfo(root).isDir()) {
            QMessageBox::warning(this, QStringLiteral("Choose a folder"),
                                 QStringLiteral("Choose an existing folder containing your documents."));
            return;
        }

        const bool pdfToWord = m_direction->currentData().toBool();
        QString libreOffice;
        if (!pdfToWord) {
            libreOffice = findLibreOffice();
            if (libreOffice.isEmpty()) {
                QMessageBox::critical(
                    this, QStringLiteral("LibreOffice not found"),
                    QStringLiteral("Word → PDF requires LibreOffice. Install it, then make sure soffice is on PATH, or use the standard installation directory."));
                return;
            }
        }

        m_convert->setEnabled(false);
        m_progress->setValue(0);
        m_progress->setFormat(QStringLiteral("Preparing…"));
        m_log->clear();
        appendLog(QStringLiteral("Input: %1").arg(root));
        appendLog(QStringLiteral("Direction: %1").arg(pdfToWord ? QStringLiteral("PDF → Word") : QStringLiteral("Word → PDF")));
        appendLog(QStringLiteral("Render DPI: %1").arg(m_dpi->currentData().toInt()));

        m_thread = new QThread(this);
        auto *worker = new BatchWorker(root, pdfToWord, m_recursive->isChecked(),
                                       m_dpi->currentData().toInt(), libreOffice);
        worker->moveToThread(m_thread);

        connect(m_thread, &QThread::started, worker, &BatchWorker::run);
        connect(worker, &BatchWorker::logMessage, this, [this](const QString &message) {
            appendLog(message);
        });
        connect(worker, &BatchWorker::progressChanged, this, [this](int done, int total) {
            if (total <= 0) {
                m_progress->setRange(0, 100);
                m_progress->setValue(0);
                m_progress->setFormat(QStringLiteral("No supported files"));
                return;
            }
            m_progress->setRange(0, total);
            m_progress->setValue(done);
            m_progress->setFormat(QStringLiteral("%1 / %2").arg(done).arg(total));
        });
        connect(worker, &BatchWorker::finished, this,
                [this](int succeeded, int failed, int total, const QString &output) {
            m_convert->setEnabled(true);
            if (total == 0) {
                m_progress->setFormat(QStringLiteral("Nothing to convert"));
                QMessageBox::information(this, QStringLiteral("No files found"),
                                         QStringLiteral("No supported documents were found."));
            } else {
                m_progress->setFormat(QStringLiteral("%1 converted, %2 failed").arg(succeeded).arg(failed));
                QMessageBox::information(this, QStringLiteral("Conversion complete"),
                    QStringLiteral("Converted: %1\nFailed: %2\n\nOutput folder:\n%3")
                        .arg(succeeded).arg(failed).arg(output));
            }
        });
        connect(worker, &BatchWorker::finished, m_thread, &QThread::quit);
        connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(m_thread, &QThread::finished, this, [this]() {
            if (m_thread) {
                m_thread->deleteLater();
                m_thread = nullptr;
            }
        });
        m_thread->start();
    }

    void appendLog(const QString &message)
    {
        m_log->appendPlainText(message);
    }

    QLineEdit *m_folder = nullptr;
    QComboBox *m_direction = nullptr;
    QComboBox *m_dpi = nullptr;
    QCheckBox *m_recursive = nullptr;
    QPushButton *m_convert = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QThread *m_thread = nullptr;
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PageConvert"));
    QApplication::setOrganizationName(QStringLiteral("mnotion"));

    MainWindow window;
    window.show();
    return app.exec();
}

#include "main.moc"
