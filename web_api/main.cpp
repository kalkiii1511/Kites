#include "common/globals.h"
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "utils/utils.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QHostAddress>
#include <QUrl>
#include <QUrlQuery>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
using namespace Kites;

QString hex64(uint64_t value) {
    return QStringLiteral("0x") + QString::number(value, 16).rightJustified(16, QLatin1Char('0'));
}

uint64_t parseAddress(const QString &text) {
    QString value = text.trimmed();
    bool ok = false;
    int base = 10;
    if (value.startsWith("0x", Qt::CaseInsensitive)) {
        value = value.mid(2);
        base = 16;
    }
    const qulonglong parsed = value.toULongLong(&ok, base);
    if (!ok) throw std::invalid_argument("Invalid memory address");
    return static_cast<uint64_t>(parsed);
}

ProcessorType parseVm(const QString &name) {
    if (name == "rvss" || name.isEmpty()) return ProcessorType::RVSS;
    if (name == "rv5-nh-nf") return ProcessorType::RV5Stage_NH_NF;
    if (name == "rv5-h-nf") return ProcessorType::RV5Stage_H_NF;
    if (name == "rv5-nh-f") return ProcessorType::RV5Stage_NH_F;
    if (name == "rv5-h-f") return ProcessorType::RV5Stage_H_F;
    throw std::invalid_argument("Unknown Kites VM type");
}

class WebApi final : public QObject {
    Q_OBJECT
public:
    explicit WebApi(QObject *parent = nullptr)
        : QObject(parent), manager_(nullptr, ProcessorType::RVSS) {
        manager_.setStepDelay(1);
        connect(&manager_, &ProcessorManager::runErrorSignal, this,
                [this](const QString &message, int) { lastRunError_ = message; });
        connect(&server_, &QTcpServer::newConnection, this, [this]() {
            while (server_.hasPendingConnections()) {
                QTcpSocket *socket = server_.nextPendingConnection();
                socket->setProperty("requestBuffer", QByteArray{});
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readRequest(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    bool listen(quint16 port) { return server_.listen(QHostAddress::Any, port); }

private:
    QTcpServer server_;
    ProcessorManager manager_;
    bool programLoaded_{false};
    bool programFinished_{false};
    QString lastRunError_;

    void readRequest(QTcpSocket *socket) {
        QByteArray buffer = socket->property("requestBuffer").toByteArray();
        buffer += socket->readAll();
        socket->setProperty("requestBuffer", buffer);
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QByteArray headers = buffer.left(headerEnd);
        int contentLength = 0;
        for (const QByteArray &line : headers.split('\n')) {
            const QByteArray lower = line.trimmed().toLower();
            if (lower.startsWith("content-length:")) contentLength = lower.mid(15).trimmed().toInt();
        }
        const qsizetype bodyStart = headerEnd + 4;
        if (buffer.size() < bodyStart + contentLength) return;
        const QByteArray requestLine = headers.left(headers.indexOf("\r\n"));
        const QList<QByteArray> parts = requestLine.split(' ');
        if (parts.size() < 2) { send(socket, 400, {{"error", "Malformed request"}}); return; }
        const QByteArray method = parts[0];
        const QUrl url = QUrl::fromEncoded(parts[1]);
        const QByteArray body = buffer.mid(bodyStart, contentLength);
        try {
            if (method == "OPTIONS") { send(socket, 204, {}); return; }
            QJsonObject result;
            int status = 200;
            if (method == "GET" && url.path() == "/api/health") {
                result = {{"ok", true}, {"engine", "Kites"}, {"vm", "rvss"}, {"programLoaded", programLoaded_}, {"programComplete", programFinished_}};
            } else if (method == "POST" && url.path() == "/api/program/assemble") {
                const auto json = parseJson(body);
                const QString vm = json.value("vm").toString("rvss");
                if (parseVm(vm) != manager_.getProcessorType()) manager_.changeProcessor(parseVm(vm));
                manager_.setStepDelay(1);
                const std::string source = json.value("source").toString().toStdString();
                if (source.empty()) throw std::invalid_argument("Assembly source is empty");
                manager_.reset();
                manager_.loadProgram(source);
                programLoaded_ = true;
                programFinished_ = false;
                result = state();
                QJsonArray instructions;
                const auto &program = manager_.getCurrentProgram();
                for (size_t i = 0; i < program.text_buffer.size(); ++i) {
                    const auto sourceLine = program.instruction_number_line_number_mapping.find(
                        static_cast<unsigned int>(i));
                    if (sourceLine == program.instruction_number_line_number_mapping.end() ||
                        i >= program.intermediate_code.size()) continue;
                    std::ostringstream instruction;
                    instruction << program.intermediate_code[i].first;
                    instructions.append(QJsonObject{
                        {"address", hex64(static_cast<uint64_t>(i) * 4)},
                        {"encoding", QStringLiteral("0x") + QString::number(program.text_buffer[i], 16).rightJustified(8, QLatin1Char('0'))},
                        {"instruction", QString::fromStdString(instruction.str())},
                        {"sourceLine", static_cast<int>(sourceLine->second)}});
                }
                result.insert("instructions", instructions);
                result.insert("assembled", true);
                result.insert("message", "Program assembled and loaded by Kites");
            } else if (method == "POST" && url.path() == "/api/simulator/step") {
                requireProgram();
                const auto json = parseJson(body);
                const int count = std::clamp(json.value("count").toInt(1), 1, 1000);
                lastRunError_.clear();
                for (int i = 0; i < count && !programFinished_; ++i) {
                    programFinished_ = manager_.step();
                    if (!lastRunError_.isEmpty()) throw std::runtime_error(lastRunError_.toStdString());
                }
                result = state();
                result.insert("message", programFinished_ ? "Program execution complete" : "Kites executed step request");
            } else if (method == "POST" && url.path() == "/api/simulator/run") {
                requireProgram();
                lastRunError_.clear();
                manager_.run();
                if (!lastRunError_.isEmpty()) throw std::runtime_error(lastRunError_.toStdString());
                programFinished_ = true;
                result = state();
                result.insert("message", "Kites run completed");
            } else if (method == "POST" && url.path() == "/api/simulator/reset") {
                manager_.reset();
                programLoaded_ = false;
                programFinished_ = false;
                result = state();
                result.insert("message", "Simulator reset");
            } else if (method == "GET" && url.path() == "/api/state") {
                result = state();
            } else if (method == "GET" && url.path() == "/api/memory") {
                QUrlQuery query(url);
                const uint64_t start = parseAddress(query.queryItemValue("address", QUrl::FullyDecoded).isEmpty()
                    ? "0x10000000" : query.queryItemValue("address", QUrl::FullyDecoded));
                const uint64_t requested = query.queryItemValue("length").isEmpty() ? 256 : query.queryItemValue("length").toULongLong();
                const uint64_t length = std::clamp<uint64_t>(requested, 1, 4096);
                if (start > std::numeric_limits<uint64_t>::max() - length) throw std::invalid_argument("Memory range overflows address space");
                QJsonArray bytes;
                auto *memory = manager_.getMemoryController();
                for (uint64_t i = 0; i < length; ++i) bytes.append(static_cast<int>(memory->peekByte(start + i)));
                result = {{"address", hex64(start)}, {"length", static_cast<qint64>(length)}, {"bytes", bytes}};
            } else if (method == "POST" && url.path() == "/api/memory/write") {
                const auto json = parseJson(body);
                const uint64_t address = parseAddress(json.value("address").toString());
                const int value = json.value("value").toInt(-1);
                if (value < 0 || value > 255) throw std::invalid_argument("Memory byte must be between 0 and 255");
                manager_.getMemoryController()->writeByte_d(address, static_cast<uint8_t>(value));
                result = {{"ok", true}, {"address", hex64(address)}, {"value", value}};
            } else {
                status = 404;
                result = {{"error", "Endpoint not found"}};
            }
            send(socket, status, result);
        } catch (const std::exception &e) {
            send(socket, 400, {{"error", QString::fromUtf8(e.what())}});
        }
    }

    static QJsonObject parseJson(const QByteArray &body) {
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            throw std::invalid_argument("Request body must be a JSON object");
        return doc.object();
    }

    void requireProgram() const {
        if (!programLoaded_) throw std::invalid_argument("Assemble a program before executing it");
    }

    QJsonObject state() const {
        const RegisterFile *registers = manager_.getRegisterFile();
        QJsonObject gpr, fpr, csr;
        for (int i = 0; i < 32; ++i) {
            gpr.insert(QString("x%1").arg(i), hex64(registers->ReadGpr(static_cast<size_t>(i))));
            fpr.insert(QString("f%1").arg(i), hex64(registers->ReadFpr(static_cast<size_t>(i))));
        }
        for (const auto &[name, address] : csr_to_address)
            csr.insert(QString::fromStdString(name), hex64(registers->ReadCsr(static_cast<size_t>(address))));
        return {{"pc", hex64(manager_.getProgramCounter())}, {"gpr", gpr}, {"fpr", fpr},
                {"csr", csr}, {"cycles", static_cast<qint64>(manager_.getCycles())},
                {"instructionsRetired", static_cast<qint64>(manager_.getInstructionsRetired())},
                {"stallCycles", static_cast<qint64>(manager_.getStallCycles())},
                {"branchMispredictions", static_cast<qint64>(manager_.getBranchMispredictions())},
                {"cpi", manager_.getCPI()}, {"ipc", manager_.getIPC()},
                {"programLoaded", programLoaded_}, {"programComplete", programFinished_}};
    }

    static void send(QTcpSocket *socket, int status, const QJsonObject &object) {
        const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QByteArray reason = "OK";
        if (status == 204) reason = "No Content";
        else if (status == 400) reason = "Bad Request";
        else if (status == 404) reason = "Not Found";
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
        response += "Content-Type: application/json; charset=utf-8\r\n";
        response += "Access-Control-Allow-Origin: *\r\n";
        response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        response += "Access-Control-Allow-Headers: Content-Type\r\n";
        response += "Connection: close\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
        response += body;
        socket->write(response);
        socket->disconnectFromHost();
    }
};
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    Kites::setupVmStateDirectory();
    WebApi api;
    const quint16 port = argc > 1 ? QString::fromLocal8Bit(argv[1]).toUShort() : 8080;
    if (!api.listen(port)) {
        qCritical("Could not start Kites Web API on port %u", port);
        return 1;
    }
    qInfo("Kites Web API listening on http://127.0.0.1:%u", port);
    return app.exec();
}

#include "main.moc"
