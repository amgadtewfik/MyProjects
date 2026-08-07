import './globals.css';
import { AuthProvider } from '@/components/AuthContext';

export const metadata = {
  title: 'Next.js + Python Auth',
  description: 'TypeScript React frontend with Python FastAPI backend authentication',
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="en">
      <body>
        <AuthProvider>{children}</AuthProvider>
      </body>
    </html>
  );
}
